/*
 * pam_wayland95_keyring - unlocks Wayland 95's password list (win95keyring)
 * with your login password, as Windows 95 unlocked your .PWL with your
 * logon password, and keeps the two the same when you change it.
 *
 *   auth      remembers the password you logged in with (after the modules
 *             before it accepted it), for the session
 *   session   hands it to win95keyring --login, run as you on your session
 *             bus, which unlocks the list (or makes it, the first time
 *             something keeps a password); then forgets it
 *   password  when your password is changed (passwd, Control Panel >
 *             Passwords): win95keyring --change-password, old and new
 *
 * Optional everywhere: it never fails a login, and does nothing for root
 * or when the user has no session bus. Added to the common-* files by
 * pam-auth-update (/usr/share/pam-configs/wayland95-keyring).
 */
#define _GNU_SOURCE
#define PAM_SM_AUTH
#define PAM_SM_SESSION
#define PAM_SM_PASSWORD

#include <security/pam_ext.h>
#include <security/pam_modules.h>

#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <pwd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>

#ifndef WIN95KEYRING_PATH
#define WIN95KEYRING_PATH "/usr/bin/win95keyring"
#endif

#define DATA_KEY "wayland95-keyring-authtok"
#define MAX_GROUPS 256

static void free_secret(pam_handle_t *pamh, void *data, int status)
{
    (void)pamh;
    (void)status;
    if (data) {
        explicit_bzero(data, strlen(data));
        free(data);
    }
}

/*
 * Runs `win95keyring MODE` as the user with DATA on its standard input,
 * detached (the login doesn't wait for it). Waits up to WAIT_SECONDS for
 * the user's session bus to appear first (systemd starts it as the
 * session opens).
 */
static int run_helper(pam_handle_t *pamh, const char *mode, const char *data, size_t len, int wait_seconds)
{
    const char *user = NULL;
    if (pam_get_user(pamh, &user, NULL) != PAM_SUCCESS || !user || !*user)
        return PAM_IGNORE;

    struct passwd pwbuf, *pw = NULL;
    char buf[4096];
    if (getpwnam_r(user, &pwbuf, buf, sizeof buf, &pw) != 0 || !pw)
        return PAM_IGNORE;
    if (pw->pw_uid == 0)
        return PAM_IGNORE;
    if (access(WIN95KEYRING_PATH, X_OK) != 0)
        return PAM_IGNORE;
    if (len >= 60000) /* well under a pipe's buffer: written before the fork */
        return PAM_IGNORE;

    const uid_t uid = pw->pw_uid;
    const gid_t gid = pw->pw_gid;
    gid_t groups[MAX_GROUPS];
    int ngroups = MAX_GROUPS;
    if (getgrouplist(pw->pw_name, gid, groups, &ngroups) < 0)
        ngroups = 0; /* too many: just the primary group */

    /* everything the child needs, made now (after fork, only
       async-signal-safe calls: the login manager may have threads) */
    char runtime[64], bus_path[96], env_runtime[96], env_bus[128], env_home[1100], env_user[300], env_logname[300];
    snprintf(runtime, sizeof runtime, "/run/user/%u", (unsigned)uid);
    snprintf(bus_path, sizeof bus_path, "%s/bus", runtime);
    snprintf(env_runtime, sizeof env_runtime, "XDG_RUNTIME_DIR=%s", runtime);
    snprintf(env_bus, sizeof env_bus, "DBUS_SESSION_BUS_ADDRESS=unix:path=%s", bus_path);
    snprintf(env_home, sizeof env_home, "HOME=%s", pw->pw_dir);
    snprintf(env_user, sizeof env_user, "USER=%s", pw->pw_name);
    snprintf(env_logname, sizeof env_logname, "LOGNAME=%s", pw->pw_name);
    char *envp[] = { env_runtime, env_bus, env_home, env_user, env_logname, (char *)"PATH=/usr/local/bin:/usr/bin:/bin", NULL };
    char *argv[] = { (char *)WIN95KEYRING_PATH, (char *)mode, NULL };

    if (wait_seconds == 0) {
        struct stat st;
        if (stat(bus_path, &st) != 0)
            return PAM_IGNORE;
    }

    int fds[2];
    if (pipe2(fds, O_CLOEXEC) != 0)
        return PAM_IGNORE;
    size_t done = 0;
    while (done < len) {
        ssize_t n = write(fds[1], data + done, len - done);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0) {
            close(fds[0]);
            close(fds[1]);
            return PAM_IGNORE;
        }
        done += (size_t)n;
    }
    close(fds[1]);

    pid_t pid = fork();
    if (pid < 0) {
        close(fds[0]);
        return PAM_IGNORE;
    }
    if (pid == 0) {
        /* the child: forks the helper and goes, so nobody waits for it */
        pid_t helper = fork();
        if (helper != 0)
            _exit(0);
        setsid();
        sigset_t none;
        sigemptyset(&none);
        sigprocmask(SIG_SETMASK, &none, NULL);
        signal(SIGPIPE, SIG_DFL);
        signal(SIGCHLD, SIG_DFL);

        if (dup2(fds[0], 0) < 0)
            _exit(127);
        int null = open("/dev/null", O_WRONLY);
        if (null >= 0) {
            dup2(null, 1);
            dup2(null, 2);
        }
        syscall(SYS_close_range, 3U, ~0U, 0U);

        /* become the user, for good */
        if (setgroups((size_t)ngroups, groups) != 0 && setgroups(0, NULL) != 0)
            _exit(127);
        if (setgid(gid) != 0 || setuid(uid) != 0)
            _exit(127);
        if (getuid() != uid || geteuid() != uid || setuid(0) == 0)
            _exit(127);

        /* the session bus comes up as the session opens */
        struct stat st;
        for (int i = 0; i < wait_seconds * 4 && stat(bus_path, &st) != 0; ++i) {
            struct timespec quarter = { 0, 250000000L };
            nanosleep(&quarter, NULL);
        }
        if (stat(bus_path, &st) != 0)
            _exit(0);
        execve(WIN95KEYRING_PATH, argv, envp);
        _exit(127);
    }
    close(fds[0]);
    while (waitpid(pid, NULL, 0) < 0 && errno == EINTR) {
    }
    return PAM_SUCCESS;
}

PAM_EXTERN int pam_sm_authenticate(pam_handle_t *pamh, int flags, int argc, const char **argv)
{
    (void)flags;
    (void)argc;
    (void)argv;
    const void *tok = NULL;
    if (pam_get_item(pamh, PAM_AUTHTOK, &tok) != PAM_SUCCESS || !tok)
        return PAM_IGNORE;
    char *copy = strdup((const char *)tok);
    if (!copy)
        return PAM_IGNORE;
    if (pam_set_data(pamh, DATA_KEY, copy, free_secret) != PAM_SUCCESS)
        free_secret(pamh, copy, 0);
    return PAM_IGNORE;
}

PAM_EXTERN int pam_sm_setcred(pam_handle_t *pamh, int flags, int argc, const char **argv)
{
    (void)pamh;
    (void)flags;
    (void)argc;
    (void)argv;
    return PAM_IGNORE;
}

PAM_EXTERN int pam_sm_open_session(pam_handle_t *pamh, int flags, int argc, const char **argv)
{
    (void)flags;
    (void)argc;
    (void)argv;
    const void *data = NULL;
    if (pam_get_data(pamh, DATA_KEY, &data) != PAM_SUCCESS || !data)
        return PAM_IGNORE;
    const char *password = (const char *)data;
    int rc = run_helper(pamh, "--login", password, strlen(password), 15);
    if (rc != PAM_SUCCESS)
        pam_syslog(pamh, LOG_DEBUG, "the password list isn't unlocked at this login");
    /* forgotten now (free_secret wipes it) */
    pam_set_data(pamh, DATA_KEY, NULL, NULL);
    return PAM_IGNORE;
}

PAM_EXTERN int pam_sm_close_session(pam_handle_t *pamh, int flags, int argc, const char **argv)
{
    (void)pamh;
    (void)flags;
    (void)argc;
    (void)argv;
    return PAM_IGNORE;
}

PAM_EXTERN int pam_sm_chauthtok(pam_handle_t *pamh, int flags, int argc, const char **argv)
{
    (void)argc;
    (void)argv;
    if (!(flags & PAM_UPDATE_AUTHTOK))
        return PAM_IGNORE;
    const void *old_tok = NULL, *new_tok = NULL;
    if (pam_get_item(pamh, PAM_OLDAUTHTOK, &old_tok) != PAM_SUCCESS || !old_tok
        || pam_get_item(pamh, PAM_AUTHTOK, &new_tok) != PAM_SUCCESS || !new_tok)
        return PAM_IGNORE;
    const size_t old_len = strlen((const char *)old_tok);
    const size_t new_len = strlen((const char *)new_tok);
    const size_t len = old_len + 1 + new_len + 1;
    char *both = malloc(len);
    if (!both)
        return PAM_IGNORE;
    memcpy(both, old_tok, old_len + 1);
    memcpy(both + old_len + 1, new_tok, new_len + 1);
    /* only into a running session (no bus: the list is changed the next
       time it's unlocked with the old password and you log in with the new) */
    run_helper(pamh, "--change-password", both, len, 0);
    explicit_bzero(both, len);
    free(both);
    return PAM_IGNORE;
}
