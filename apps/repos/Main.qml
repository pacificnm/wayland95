import QtQuick
import QtQuick.Window
import Win95Ui
import Win95Repos

// Repositories: every GitHub repository you can reach (yours and your
// organisations'), and the clones of them on this computer. On the left,
// the repositories by owner; on the right, the one chosen: Overview,
// Changes (commit), History, Issues and Pull Requests. Fetch, Pull and
// Push on the toolbar; Clone, New, Rename, Visibility, Archive and Delete
// in the menus. Everything runs through `gh` (its sign-in) and `git`.
Window {
    id: window

    width: 940
    height: 620
    minimumWidth: 720
    minimumHeight: 440
    visible: true
    color: Theme.face
    title: current ? current.display + " - Repositories" : "Repositories"

    // ------------------------------------------------------------ state
    property bool ghReady: false      // gh is installed
    property bool signedIn: false
    property string login: ""
    property var orgs: []
    property var remote: []           // gh's repositories
    property var locals: []           // clones (and other git repositories) here
    property var items: []            // the list: remote and local merged
    property string currentId: ""
    readonly property var current: {
        for (var i = 0; i < items.length; ++i)
            if (items[i].id === currentId)
                return items[i];
        return null;
    }
    property string message: ""       // the status bar's last word
    property bool loadingRemote: false

    // the current repository's clone
    property var status: ({ branch: "", upstream: "", ahead: 0, behind: 0, files: [], noCommits: false, detached: false })
    property var branches: []
    property var checked: ({})        // path -> false when left out of the commit
    property string selectedFile: ""
    property string fileDiff: ""
    property var commits: []
    property string commitText: ""
    property int commitRow: -1
    property var issues: []
    property var pulls: []
    property string issueState: "open"
    property bool loadingIssues: false

    readonly property string cloneFolder: Hub.expandPath(String(Hub.setting("cloneFolder", Hub.homePath())))
    readonly property var scanFolders: {
        var v = Hub.setting("scanFolders", "");
        var list = String(v).split("\n").filter(function (s) { return s.trim().length; });
        return list.length ? list : [Hub.homePath()];
    }
    readonly property bool modalOpen: popup.visible || msgBox.visible || cloneDialog.visible || newDialog.visible
                                      || renameDialog.visible || deleteDialog.visible || optionsDialog.visible
                                      || describeDialog.visible

    // -------------------------------------------------------- helpers
    function log(text) { if (reposDebug) console.log("repos: " + text); }
    function say(text) { message = text; log("say " + text); }
    function trimError(text) {
        text = String(text || "").trim();
        return text.length > 700 ? text.slice(0, 700) + "..." : text;
    }
    function fail(title, err, then) {
        msgBox.show(title, trimError(err) || "It didn't work, and gave no reason.", "error", ["OK"], then || null);
    }
    function gh(args, cb, input) { Hub.run("gh", args, "", cb, input); }
    function git(path, args, cb) { Hub.run("git", args, path, cb); }
    function plural(n, one, many) { return n + " " + (n === 1 ? one : many); }
    function ago(iso) {
        if (!iso)
            return "";
        var s = (Date.now() - new Date(iso).getTime()) / 1000;
        if (s < 90) return "just now";
        if (s < 5400) return Math.round(s / 60) + " minutes ago";
        if (s < 129600) return Math.round(s / 3600) + " hours ago";
        if (s < 3888000) return Math.round(s / 86400) + " days ago";
        if (s < 47304000) return Math.round(s / 2592000) + " months ago";
        return Math.round(s / 31536000) + " years ago";
    }

    // ----------------------------------------------------------- loading
    function refresh() {
        say("Looking for your repositories...");
        Hub.run("gh", ["--version"], "", function (code) {
            ghReady = code === 0;
            if (!ghReady) {
                signedIn = false;
                remote = [];
                merge();
                say("The gh command isn't installed: only the repositories on this computer are shown.");
                return;
            }
            gh(["api", "user", "--jq", ".login"], function (code2, out) {
                signedIn = code2 === 0 && out.trim().length > 0;
                login = signedIn ? out.trim() : "";
                if (signedIn)
                    loadRemote();
                else {
                    remote = [];
                    merge();
                    say("Not signed in to GitHub: only the repositories on this computer are shown.");
                }
            });
        });
        scanLocal();
    }

    readonly property var repoFields: "name,nameWithOwner,owner,description,visibility,isFork,isArchived,pushedAt,url,defaultBranchRef,stargazerCount,forkCount,primaryLanguage,parent"
    function loadRemote() {
        loadingRemote = true;
        gh(["api", "user/orgs", "--jq", ".[].login"], function (code, out) {
            orgs = code === 0 ? out.split("\n").filter(function (s) { return s.length; }) : [];
            var owners = [login].concat(orgs), got = [], left = owners.length, errors = [];
            owners.forEach(function (owner) {
                gh(["repo", "list", owner, "--limit", "1000", "--json", repoFields], function (c, o, e) {
                    if (c === 0) {
                        try { got = got.concat(JSON.parse(o)); } catch (x) { errors.push(owner + ": " + x); }
                    } else {
                        errors.push(owner + ": " + e.trim());
                    }
                    if (--left === 0) {
                        loadingRemote = false;
                        remote = got;
                        merge();
                        say(errors.length ? "Some repositories couldn't be listed: " + errors.join("; ")
                                          : plural(got.length, "repository", "repositories") + " on GitHub");
                    }
                });
            });
        });
    }

    function scanLocal() {
        Hub.scanLocal(scanFolders, 3, function (found) {
            var extra = String(Hub.setting("extraFolders", "")).split("\n").filter(function (s) { return s.length; });
            extra.forEach(function (p) {
                var r = Hub.localRepo(p);
                if (r.path && !found.some(function (f) { return f.path === r.path; }))
                    found.push(r);
            });
            locals = found;
            merge();
        });
    }

    // one list: GitHub's repositories (with their clone, if any), then the
    // repositories only on this computer
    function merge() {
        var bySlug = {};
        locals.forEach(function (l) {
            if (l.slug && !bySlug[l.slug.toLowerCase()])
                bySlug[l.slug.toLowerCase()] = l.path;
        });
        var out = [], seen = {};
        remote.forEach(function (r) {
            var slug = r.nameWithOwner;
            seen[slug.toLowerCase()] = true;
            var owner = r.owner ? r.owner.login : slug.split("/")[0];
            out.push({
                id: slug, slug: slug, name: r.name, owner: owner, display: slug,
                group: owner === login ? "0 " + owner : "1 " + owner,
                description: r.description || "", visibility: (r.visibility || "").toLowerCase(),
                fork: !!r.isFork, archived: !!r.isArchived, parent: r.parent ? (r.parent.owner ? r.parent.owner.login + "/" : "") + r.parent.name : "",
                url: r.url, pushedAt: r.pushedAt, stars: r.stargazerCount || 0, forks: r.forkCount || 0,
                language: r.primaryLanguage ? r.primaryLanguage.name : "",
                defaultBranch: r.defaultBranchRef ? r.defaultBranchRef.name : "",
                path: bySlug[slug.toLowerCase()] || "",
            });
        });
        locals.forEach(function (l) {
            if (l.slug && seen[l.slug.toLowerCase()])
                return;
            out.push({
                id: l.path, slug: l.slug, name: l.name, owner: "", display: l.slug || l.name,
                group: "2 On this computer only", description: l.slug ? "A clone of " + l.slug + " (not one of your repositories)" : "",
                visibility: "", fork: false, archived: false, parent: "", url: l.slug ? "https://github.com/" + l.slug : "",
                pushedAt: "", stars: 0, forks: 0, language: "", defaultBranch: "", path: l.path,
            });
        });
        out.sort(function (a, b) {
            return a.group === b.group ? a.name.toLowerCase().localeCompare(b.name.toLowerCase()) : a.group < b.group ? -1 : 1;
        });
        items = out;
        log("list " + out.map(function (it) { return it.display + (it.path ? "*" : ""); }).join(","));
        if (currentId && !current)
            currentId = "";
        else if (current)
            loadCurrent();
    }

    readonly property var shown: {
        var f = filter.text.trim().toLowerCase();
        return items.filter(function (it) {
            if (clonedOnly.checked && !it.path)
                return false;
            return !f || it.display.toLowerCase().indexOf(f) >= 0 || it.description.toLowerCase().indexOf(f) >= 0;
        });
    }

    function choose(id) {
        if (id === currentId)
            return;
        currentId = id;
        log("chose " + id);
        status = { branch: "", upstream: "", ahead: 0, behind: 0, files: [], noCommits: false, detached: false };
        branches = [];
        commits = [];
        commitText = "";
        commitRow = -1;
        issues = [];
        pulls = [];
        selectedFile = "";
        fileDiff = "";
        loadCurrent();
    }

    function loadCurrent() {
        if (!current)
            return;
        if (current.path) {
            loadStatus();
            loadHistory();
        }
        if (tabs.currentIndex >= 3)
            loadIssues();
    }

    // ------------------------------------------------------------- local
    function loadStatus(then) {
        var repo = current;
        if (!repo || !repo.path)
            return;
        git(repo.path, ["status", "--porcelain=v1", "-b", "-z", "--untracked-files=all"], function (code, out, err) {
            if (!current || current.id !== repo.id)
                return;
            if (code !== 0) {
                say("git status: " + trimError(err));
                return;
            }
            var parts = out.split("\0"), s = { branch: "", upstream: "", ahead: 0, behind: 0, files: [], noCommits: false, detached: false };
            for (var i = 0; i < parts.length; ++i) {
                var p = parts[i];
                if (!p.length)
                    continue;
                if (p.startsWith("## ")) {
                    var head = p.slice(3);
                    var m;
                    if ((m = head.match(/^No commits yet on (.+)$/)) || (m = head.match(/^Initial commit on (.+)$/))) {
                        s.branch = m[1]; s.noCommits = true;
                    } else if (head.startsWith("HEAD (no branch)")) {
                        s.detached = true; s.branch = "(no branch)";
                    } else {
                        m = head.match(/^(.+?)(?:\.\.\.(\S+))?(?: \[(.*)\])?$/);
                        s.branch = m[1];
                        s.upstream = m[2] || "";
                        var counts = m[3] || "";
                        var a = counts.match(/ahead (\d+)/), b = counts.match(/behind (\d+)/);
                        s.ahead = a ? Number(a[1]) : 0;
                        s.behind = b ? Number(b[1]) : 0;
                    }
                    continue;
                }
                var code2 = p.slice(0, 2), path = p.slice(3);
                if (code2[0] === "R" || code2[0] === "C")
                    ++i; // (the name it had)
                var kind = code2 === "??" ? "new" : (code2.indexOf("D") >= 0 ? "deleted" : code2.indexOf("A") >= 0 ? "new"
                          : code2.indexOf("R") >= 0 ? "renamed" : code2.indexOf("U") >= 0 ? "conflict" : "modified");
                s.files.push({ path: path, kind: kind });
            }
            s.files.sort(function (x, y) { return x.path.localeCompare(y.path); });
            status = s;
            log("status " + repo.display + " " + s.branch + " upstream=" + (s.upstream || "-") + " ahead=" + s.ahead
                + " behind=" + s.behind + " files=" + s.files.map(function (f) { return f.kind + ":" + f.path; }).join(","));
            if (selectedFile && !s.files.some(function (f) { return f.path === selectedFile; })) {
                selectedFile = "";
                fileDiff = "";
            }
            if (then)
                then();
        });
        git(repo.path, ["for-each-ref", "--format=%(refname:short)", "refs/heads"], function (code, out) {
            if (current && current.id === repo.id)
                branches = code === 0 ? out.split("\n").filter(function (b) { return b.length; }) : [];
        });
    }

    function showDiff(file) {
        selectedFile = file.path;
        var repo = current;
        if (file.kind === "new" && !status.noCommits) {
            git(repo.path, ["ls-files", "--error-unmatch", "--", file.path], function (tracked) {
                if (tracked !== 0) {
                    var t = Hub.readText(Hub.joinPath(repo.path, file.path));
                    fileDiff = t.length ? t.split("\n").map(function (l) { return "+" + l; }).join("\n")
                                        : "(a new file that isn't text)";
                    return;
                }
                diffOf(repo, file.path);
            });
            return;
        }
        if (status.noCommits) {
            var text = Hub.readText(Hub.joinPath(repo.path, file.path));
            fileDiff = text.length ? text.split("\n").map(function (l) { return "+" + l; }).join("\n") : "(not text)";
            return;
        }
        diffOf(repo, file.path);
    }
    function diffOf(repo, path) {
        git(repo.path, ["diff", "HEAD", "--", path], function (code, out, err) {
            if (selectedFile === path)
                fileDiff = code === 0 ? (out.length ? out : "(no changes in its text)") : trimError(err);
        });
    }

    function commitPaths() {
        return status.files.filter(function (f) { return checked[f.path] !== false; }).map(function (f) { return f.path; });
    }
    function commit() {
        var repo = current, paths = commitPaths(), summary = summaryField.text.trim();
        if (!summary || !paths.length)
            return;
        var description = descriptionField.text.trim();
        say("Committing " + plural(paths.length, "file", "files") + "...");
        git(repo.path, ["add", "-A", "--"].concat(paths), function (code, out, err) {
            if (code !== 0) {
                fail("Commit", err);
                return;
            }
            var args = ["commit", "-m", summary];
            if (description)
                args.push("-m", description);
            args.push("--");
            git(repo.path, args.concat(paths), function (c2, o2, e2) {
                if (c2 !== 0) {
                    if (/tell me who you are|user\.email|user\.name/i.test(e2))
                        fail("Commit", "git doesn't know your name and email yet, which every commit records. "
                             + "Set them in a terminal:\n\ngit config --global user.name \"Your Name\"\n"
                             + "git config --global user.email you@example.com");
                    else
                        fail("Commit", e2 || o2);
                    return;
                }
                summaryField.text = "";
                descriptionField.text = "";
                checked = {};
                say("Committed to " + status.branch + ".");
                loadStatus();
                loadHistory();
            });
        });
    }

    function fetch() {
        var repo = current;
        if (!repo || !repo.path)
            return;
        say("Fetching " + repo.display + "...");
        git(repo.path, ["fetch", "--prune"], function (code, out, err) {
            if (code !== 0) {
                remoteFailure("Fetch", err, fetch);
                return;
            }
            say("Fetched " + repo.display + ".");
            loadStatus();
        });
    }
    function pull() {
        var repo = current;
        if (!repo || !repo.path)
            return;
        say("Pulling " + repo.display + "...");
        git(repo.path, ["pull", "--ff-only"], function (code, out, err) {
            if (code !== 0) {
                if (/Not possible to fast-forward|diverg/i.test(err))
                    fail("Pull", "Your branch and the one on GitHub have both changed since they were last the same, "
                         + "so they can't simply be lined up. Merge or rebase them in a terminal (Repository > Open Terminal Here).");
                else
                    remoteFailure("Pull", err, pull);
                return;
            }
            say("Pulled " + repo.display + ".");
            loadStatus();
            loadHistory();
        });
    }
    function push() {
        var repo = current;
        if (!repo || !repo.path || status.noCommits)
            return;
        say("Pushing " + repo.display + "...");
        var args = status.upstream ? ["push"] : ["push", "-u", "origin", "HEAD"];
        git(repo.path, args, function (code, out, err) {
            if (code !== 0) {
                if (/rejected|fetch first|non-fast-forward/i.test(err))
                    fail("Push", "GitHub has commits this copy doesn't. Pull first, then push.");
                else
                    remoteFailure("Push", err, push);
                return;
            }
            say("Pushed " + repo.display + ".");
            loadStatus();
        });
    }
    // git asking for a password it can't get: offer gh's sign-in for git
    function remoteFailure(what, err, retry) {
        if (/could not read Username|terminal prompts disabled|Authentication failed|Permission denied/i.test(err) && signedIn) {
            msgBox.show(what, "git couldn't sign in to GitHub.\n\nUse your GitHub sign-in (from gh) for git too? "
                        + "This runs 'gh auth setup-git'.", "question", ["Yes", "No"], function (i) {
                if (i !== 0)
                    return;
                gh(["auth", "setup-git"], function (code, out, e2) {
                    if (code !== 0)
                        fail(what, e2);
                    else
                        retry();
                });
            });
            return;
        }
        fail(what, err);
    }

    function switchBranch(name) {
        var repo = current;
        if (!repo || name === status.branch)
            return;
        git(repo.path, ["switch", name], function (code, out, err) {
            if (code !== 0)
                fail("Switch Branch", err);
            else
                say("Switched to " + name + ".");
            loadStatus();
            loadHistory();
        });
    }

    function loadHistory() {
        var repo = current;
        if (!repo || !repo.path)
            return;
        git(repo.path, ["log", "-n", "300", "--date=relative", "--format=%h%x1f%an%x1f%ad%x1f%s%x1f%H"], function (code, out) {
            if (!current || current.id !== repo.id)
                return;
            commits = code !== 0 ? [] : out.split("\n").filter(function (l) { return l.length; }).map(function (l) {
                var f = l.split("\x1f");
                return { short: f[0], author: f[1], when: f[2], subject: f[3], hash: f[4] };
            });
        });
    }
    function showCommit(row) {
        commitRow = row;
        var c = commits[row];
        git(current.path, ["show", "--stat", "--patch", "--format=fuller", c.hash], function (code, out, err) {
            if (commitRow === row)
                commitText = code === 0 ? out : trimError(err);
        });
    }

    // ------------------------------------------------- issues and pulls
    function loadIssues() {
        var repo = current;
        if (!repo || !repo.slug || !signedIn)
            return;
        loadingIssues = true;
        var left = 2;
        function done() { if (--left === 0) loadingIssues = false; }
        gh(["issue", "list", "-R", repo.slug, "--state", issueState, "--limit", "200",
            "--json", "number,title,author,updatedAt,labels,url,comments"], function (code, out) {
            if (current && current.id === repo.id)
                issues = code === 0 ? JSON.parse(out) : [];
            log("issues " + repo.slug + " " + issues.length);
            done();
        });
        gh(["pr", "list", "-R", repo.slug, "--state", issueState, "--limit", "200",
            "--json", "number,title,author,updatedAt,headRefName,isDraft,url"], function (code, out) {
            if (current && current.id === repo.id)
                pulls = code === 0 ? JSON.parse(out) : [];
            log("pulls " + repo.slug + " " + pulls.length);
            done();
        });
    }

    // ------------------------------------------------- managing them
    function cloneTo(slug, folder) {
        var dest = Hub.joinPath(folder, slug.split("/")[1]);
        if (Hub.exists(dest)) {
            fail("Clone", "There's already something at " + dest + ". Choose another folder.");
            return;
        }
        say("Cloning " + slug + " into " + dest + "...");
        gh(["repo", "clone", slug, dest], function (code, out, err) {
            if (code !== 0) {
                fail("Clone", err);
                return;
            }
            say("Cloned " + slug + " into " + dest + ".");
            var extra = String(Hub.setting("extraFolders", "")).split("\n").filter(function (s) { return s.length; });
            if (extra.indexOf(dest) < 0) {
                extra.push(dest);
                Hub.setSetting("extraFolders", extra.join("\n"));
            }
            currentId = slug;
            scanLocal();
        });
    }

    function createRepo(name, description, isPrivate, readme, cloneIt, source) {
        var args = ["repo", "create", name, isPrivate ? "--private" : "--public"];
        if (description)
            args.push("--description", description);
        if (source)
            args.push("--source", source, "--remote", "origin", "--push");
        else if (readme)
            args.push("--add-readme");
        say("Creating " + name + " on GitHub...");
        gh(args, function (code, out, err) {
            if (code !== 0) {
                fail("New Repository", err);
                return;
            }
            var slug = name.indexOf("/") >= 0 ? name : login + "/" + name;
            say("Created " + slug + ".");
            currentId = slug;
            if (cloneIt && !source)
                cloneTo(slug, cloneFolder);
            refresh();
        });
    }

    function renameRepo(newName) {
        var repo = current;
        gh(["repo", "rename", newName, "-R", repo.slug, "--yes"], function (code, out, err) {
            if (code !== 0) {
                fail("Rename", err);
                return;
            }
            var slug = repo.owner + "/" + newName;
            // the clone's origin follows (GitHub redirects the old address, but not for ever)
            if (repo.path)
                git(repo.path, ["remote", "set-url", "origin", "https://github.com/" + slug + ".git"], function () { scanLocal(); });
            say("Renamed " + repo.slug + " to " + slug + ".");
            currentId = slug;
            loadRemote();
        });
    }

    function setVisibility(makePrivate) {
        var repo = current;
        msgBox.show("Change Visibility", makePrivate
                    ? "Make " + repo.slug + " private? Only you and the people you choose will see it. Stars and watchers from others are lost."
                    : "Make " + repo.slug + " public? Anyone on the internet will be able to see it and its history.",
                    "question", ["Yes", "No"], function (i) {
            if (i !== 0)
                return;
            var args = ["repo", "edit", repo.slug, "--visibility", makePrivate ? "private" : "public"];
            function done(code, out, err) {
                if (code !== 0)
                    fail("Change Visibility", err);
                else {
                    say(repo.slug + " is " + (makePrivate ? "private" : "public") + " now.");
                    loadRemote();
                }
            }
            // newer gh wants this said; older gh (2.46, Debian 13's) doesn't know it
            gh(args.concat(["--accept-visibility-change-consequences"]), function (code, out, err) {
                if (code !== 0 && /unknown flag/.test(err))
                    gh(args, done);
                else
                    done(code, out, err);
            });
        });
    }

    function setArchived(archive) {
        var repo = current;
        msgBox.show(archive ? "Archive" : "Unarchive", archive
                    ? "Archive " + repo.slug + "? It becomes read-only: no pushes, issues or pull requests until it's unarchived."
                    : "Unarchive " + repo.slug + "? It can be changed again.",
                    "question", ["Yes", "No"], function (i) {
            if (i !== 0)
                return;
            gh(["repo", archive ? "archive" : "unarchive", repo.slug, "--yes"], function (code, out, err) {
                if (code !== 0)
                    fail(archive ? "Archive" : "Unarchive", err);
                else {
                    say(repo.slug + (archive ? " is archived." : " isn't archived any more."));
                    loadRemote();
                }
            });
        });
    }

    function deleteRepo() {
        var repo = current;
        gh(["repo", "delete", repo.slug, "--yes"], function (code, out, err) {
            if (code !== 0) {
                if (/delete_repo/.test(err))
                    msgBox.show("Delete", "Your GitHub sign-in isn't allowed to delete repositories yet.\n\n"
                                + "Allow it? A terminal opens to ask GitHub (in your browser); then choose Delete again.",
                                "question", ["Yes", "No"], function (i) {
                        if (i === 0)
                            Hub.openTerminal(Hub.homePath(), ["gh", "auth", "refresh", "-h", "github.com", "-s", "delete_repo"]);
                    });
                else
                    fail("Delete", err);
                return;
            }
            say("Deleted " + repo.slug + " from GitHub." + (repo.path ? " The copy at " + repo.path + " is still here." : ""));
            currentId = repo.path ? repo.path : "";
            loadRemote();
        });
    }

    function editDescription(text) {
        var repo = current;
        gh(["repo", "edit", repo.slug, "--description", text], function (code, out, err) {
            if (code !== 0)
                fail("Description", err);
            else
                loadRemote();
        });
    }

    function addLocalFolder(path) {
        var r = Hub.localRepo(path);
        if (!r.path) {
            fail("Add Local Repository", path + " isn't a git repository (it has no .git folder).");
            return;
        }
        var extra = String(Hub.setting("extraFolders", "")).split("\n").filter(function (s) { return s.length; });
        if (extra.indexOf(r.path) < 0) {
            extra.push(r.path);
            Hub.setSetting("extraFolders", extra.join("\n"));
        }
        currentId = r.slug && remote.some(function (x) { return x.nameWithOwner.toLowerCase() === r.slug.toLowerCase(); }) ? "" : r.path;
        scanLocal();
    }

    Component.onCompleted: refresh()

    // ------------------------------------------------------------- menus
    function fileMenu() {
        return [
            { text: "New Repository...", shortcut: "Ctrl+N", enabled: signedIn, action: function () { newDialog.open(""); } },
            { text: "Clone Repository...", shortcut: "Ctrl+Shift+O", action: function () { cloneDialog.open(current && !current.path ? current.slug : ""); } },
            { text: "Add Local Repository...", action: function () { localDialog.openAt(Hub.homePath(), ""); } },
            { separator: true },
            { text: "Options...", action: function () { optionsDialog.open(); } },
            { text: "Refresh", shortcut: "F5", action: window.refresh },
            { separator: true },
            { text: "Exit", action: window.close },
        ];
    }
    function repoMenu() {
        var r = current, local = !!(r && r.path), onGitHub = !!(r && r.slug && remote.some(function (x) { return x.nameWithOwner === r.slug; }));
        return [
            { text: "&Fetch", enabled: local, action: window.fetch },
            { text: "P&ull", shortcut: "Ctrl+Shift+P", enabled: local && !!status.upstream, action: window.pull },
            { text: "&Push", shortcut: "Ctrl+P", enabled: local && !status.noCommits, action: window.push },
            { separator: true },
            { text: "Open in File &Manager", enabled: local, action: function () { Hub.openFolder(r.path); } },
            { text: "Open &Terminal Here", enabled: local, action: function () { Hub.openTerminal(r.path); } },
            { text: "&View on GitHub", enabled: !!(r && r.url), action: function () { Hub.openUrl(r.url); } },
            { separator: true },
            { text: "&Clone...", enabled: !!(r && r.slug && !local), action: function () { cloneDialog.open(r.slug); } },
            { text: "Pu&blish to GitHub...", enabled: !!(r && local && !r.slug) && signedIn, action: function () { newDialog.open(r.path); } },
            { separator: true },
            { text: "&Edit Description...", enabled: onGitHub, action: function () { describeDialog.open(); } },
            { text: "&Rename...", enabled: onGitHub, action: function () { renameDialog.open(); } },
            { text: r && r.visibility === "private" ? "Make Publ&ic..." : "Make Pr&ivate...", enabled: onGitHub,
              action: function () { window.setVisibility(r.visibility !== "private"); } },
            { text: r && r.archived ? "Un&archive..." : "&Archive...", enabled: onGitHub,
              action: function () { window.setArchived(!r.archived); } },
            { text: "&Delete...", enabled: onGitHub, action: function () { deleteDialog.open(); } },
        ];
    }
    function helpMenu() {
        return [
            { text: "About Repositories", action: function () {
                msgBox.show("About Repositories", "Repositories\n\nYour GitHub repositories and their copies on this computer,\n"
                            + "through the gh and git commands (it uses gh's sign-in).\n\nVersion " + Wayland95Settings.version,
                            "info", ["OK"], null);
            } },
        ];
    }

    Shortcut { sequence: "F5"; enabled: !window.modalOpen; onActivated: window.refresh() }
    Shortcut { sequence: "Ctrl+N"; enabled: !window.modalOpen && window.signedIn; onActivated: newDialog.open("") }
    Shortcut { sequence: "Ctrl+Shift+O"; enabled: !window.modalOpen; onActivated: cloneDialog.open("") }
    Shortcut { sequence: "Ctrl+P"; enabled: !window.modalOpen; onActivated: window.push() }
    Shortcut { sequence: "Ctrl+Shift+P"; enabled: !window.modalOpen; onActivated: window.pull() }

    MenuBar95 {
        id: menuBar
        x: 0
        y: 0
        width: parent.width
        height: 20
        popupMenu: popup
        menus: [
            { title: "File", items: window.fileMenu },
            { title: "Repository", items: window.repoMenu },
            { title: "Help", items: window.helpMenu },
        ]
    }

    // ----------------------------------------------------------- toolbar
    Row {
        id: toolbar
        x: 4
        y: menuBar.height + 2
        height: 24
        spacing: 2
        readonly property bool local: !!(window.current && window.current.path)
        ToolButton95 { text: "Refresh"; onClicked: window.refresh() }
        Item { width: 6; height: 1 }
        ToolButton95 { text: "Clone..."; onClicked: cloneDialog.open(window.current && !window.current.path ? window.current.slug : "") }
        ToolButton95 { text: "New..."; enabled: window.signedIn; onClicked: newDialog.open("") }
        Item { width: 6; height: 1 }
        ToolButton95 { text: "Fetch"; enabled: toolbar.local; onClicked: window.fetch() }
        ToolButton95 {
            text: window.status.behind > 0 ? "Pull (" + window.status.behind + ")" : "Pull"
            enabled: toolbar.local && !!window.status.upstream
            onClicked: window.pull()
        }
        ToolButton95 {
            text: window.status.ahead > 0 ? "Push (" + window.status.ahead + ")" : (toolbar.local && !window.status.upstream && !window.status.noCommits ? "Publish Branch" : "Push")
            enabled: toolbar.local && !window.status.noCommits
            onClicked: window.push()
        }
        Item { width: 6; height: 1 }
        ToolButton95 { text: "Open Folder"; enabled: toolbar.local; onClicked: Hub.openFolder(window.current.path) }
        ToolButton95 { text: "Terminal"; enabled: toolbar.local; onClicked: Hub.openTerminal(window.current.path) }
        ToolButton95 { text: "View on GitHub"; enabled: !!(window.current && window.current.url); onClicked: Hub.openUrl(window.current.url) }
    }

    // --------------------------------------------- the repositories list
    Item {
        id: left
        x: 4
        y: toolbar.y + toolbar.height + 4
        width: 270
        height: parent.height - y - statusBar.height - 2

        TextField95 {
            id: filter
            width: parent.width
        }
        Text {
            visible: !filter.text && !filter.activeFocus
            x: 6
            y: 4
            text: "Filter"
            color: Theme.shadow
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSize
        }
        CheckBox95 {
            id: clonedOnly
            y: 26
            text: "On this computer only"
            onToggled: (on) => { checked = on; }
        }

        Bevel {
            y: 48
            width: parent.width
            height: parent.height - y
            style: "sunken"
            face: Theme.window

            ListView {
                id: repoList
                x: 2
                y: 2
                width: parent.width - 4 - (repoScroll.visible ? repoScroll.width : 0)
                height: parent.height - 4
                clip: true
                model: window.shown
                boundsBehavior: Flickable.StopAtBounds
                activeFocusOnTab: true
                section.property: "group"
                section.delegate: Rectangle {
                    required property string section
                    width: repoList.width
                    height: 20
                    color: Theme.face
                    Text {
                        x: 4
                        anchors.verticalCenter: parent.verticalCenter
                        text: parent.section.slice(2)
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSize
                        font.bold: true
                    }
                    Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.shadow }
                }
                delegate: Item {
                    id: repoRow
                    required property var modelData
                    required property int index
                    readonly property bool chosen: modelData.id === window.currentId
                    width: repoList.width
                    height: 20
                    Rectangle {
                        anchors.fill: parent
                        color: repoRow.chosen ? Theme.selection : "transparent"
                    }
                    RepoIcon {
                        x: 4
                        anchors.verticalCenter: parent.verticalCenter
                        kind: !repoRow.modelData.owner ? "local" : repoRow.modelData.archived ? "archive"
                            : repoRow.modelData.fork ? "fork" : repoRow.modelData.visibility === "private" ? "private" : "repo"
                    }
                    Text {
                        x: 24
                        width: parent.width - x - (cloneMark.visible ? 18 : 4)
                        anchors.verticalCenter: parent.verticalCenter
                        text: repoRow.modelData.owner ? repoRow.modelData.name : repoRow.modelData.display
                        elide: Text.ElideRight
                        color: repoRow.chosen ? Theme.selectionText : (repoRow.modelData.archived ? Theme.shadow : Theme.text)
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSize
                    }
                    // a clone of it is on this computer
                    Rectangle {
                        id: cloneMark
                        visible: !!repoRow.modelData.path && !!repoRow.modelData.owner
                        anchors.right: parent.right
                        anchors.rightMargin: 5
                        anchors.verticalCenter: parent.verticalCenter
                        width: 8
                        height: 8
                        radius: 4
                        color: "#00a000"
                        border.color: "#004000"
                    }
                    MouseArea {
                        anchors.fill: parent
                        acceptedButtons: Qt.LeftButton | Qt.RightButton
                        onPressed: (mouse) => {
                            repoList.forceActiveFocus();
                            repoList.currentIndex = repoRow.index;
                            window.choose(repoRow.modelData.id);
                            if (mouse.button === Qt.RightButton) {
                                var p = mapToItem(popup, mouse.x, mouse.y);
                                popup.popup(p.x, p.y, window.repoMenu());
                            }
                        }
                        onDoubleClicked: {
                            if (repoRow.modelData.path)
                                Hub.openFolder(repoRow.modelData.path);
                            else if (repoRow.modelData.slug)
                                cloneDialog.open(repoRow.modelData.slug);
                        }
                    }
                }
                Keys.onUpPressed: { decrementCurrentIndex(); if (currentIndex >= 0) window.choose(window.shown[currentIndex].id); }
                Keys.onDownPressed: { incrementCurrentIndex(); if (currentIndex >= 0) window.choose(window.shown[currentIndex].id); }

                Text {
                    visible: repoList.count === 0
                    anchors.centerIn: parent
                    width: parent.width - 20
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    text: window.loadingRemote || Hub.busy ? "Looking..." : filter.text ? "None match." : "No repositories found."
                    color: Theme.shadow
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSize
                }
            }
            ScrollBar95 {
                id: repoScroll
                anchors.right: parent.right
                anchors.rightMargin: 2
                y: 2
                height: parent.height - 4
                flickable: repoList
                stepSize: 20
                visible: needed
            }
        }
    }

    // ---------------------------------------------- the one chosen
    Item {
        id: right
        x: left.x + left.width + 8
        y: left.y
        width: parent.width - x - 4
        height: left.height

        // not signed in: how to
        Column {
            visible: !window.current
            anchors.centerIn: parent
            width: Math.min(parent.width - 40, 420)
            spacing: 12
            Text {
                width: parent.width
                wrapMode: Text.WordWrap
                horizontalAlignment: Text.AlignHCenter
                text: !window.ghReady ? "The gh command (GitHub's own) isn't installed, so only the repositories on this computer are listed. "
                                        + "Install it with: sudo apt install gh"
                    : !window.signedIn ? "Sign in to GitHub to see all your repositories, their issues and pull requests."
                    : "Choose a repository on the left."
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSize
            }
            Button95 {
                visible: window.ghReady && !window.signedIn
                anchors.horizontalCenter: parent.horizontalCenter
                width: 160
                text: "Sign In to GitHub..."
                onClicked: {
                    Hub.openTerminal(Hub.homePath(), ["gh", "auth", "login", "--web", "--git-protocol", "https"]);
                    window.say("When you've signed in in the terminal, press F5 (Refresh).");
                }
            }
        }

        Item {
            visible: !!window.current
            anchors.fill: parent

            // its name and what it is
            Row {
                id: heading
                spacing: 8
                RepoIcon {
                    anchors.verticalCenter: parent.verticalCenter
                    size: 32
                    kind: !window.current ? "repo" : !window.current.owner ? "local" : window.current.archived ? "archive"
                        : window.current.fork ? "fork" : window.current.visibility === "private" ? "private" : "repo"
                }
                Column {
                    Text {
                        text: window.current ? window.current.display : ""
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSize + 5
                        font.bold: true
                    }
                    Text {
                        width: right.width - 50
                        elide: Text.ElideRight
                        text: {
                            var r = window.current;
                            if (!r) return "";
                            var bits = [];
                            if (r.visibility) bits.push(r.visibility === "private" ? "Private" : r.visibility === "internal" ? "Internal" : "Public");
                            if (r.fork) bits.push("Fork" + (r.parent ? " of " + r.parent : ""));
                            if (r.archived) bits.push("Archived");
                            if (r.path) bits.push("On this computer");
                            if (!r.owner && !r.slug) bits.push("Not on GitHub");
                            return bits.join("  ·  ") + (r.description ? "  —  " + r.description : "");
                        }
                        color: Theme.shadow
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSize
                    }
                }
            }

            TabWidget95 {
                id: tabs
                y: heading.height + 8
                width: parent.width
                height: parent.height - y
                titles: ["Overview", window.status.files.length ? "Changes (" + window.status.files.length + ")" : "Changes",
                         "History", "Issues", "Pull Requests"]
                onCurrentIndexChanged: if (currentIndex >= 3 && !window.issues.length && !window.pulls.length) window.loadIssues()

                // ------------------------------------------------ Overview
                Item {
                    Grid {
                        x: 14
                        y: 14
                        columns: 2
                        columnSpacing: 14
                        rowSpacing: 8
                        Repeater {
                            model: {
                                var r = window.current, s = window.status;
                                if (!r) return [];
                                var rows = [];
                                if (r.owner) {
                                    rows.push(["Visibility:", r.visibility === "private" ? "Private" : r.visibility === "internal" ? "Internal" : "Public"]);
                                    rows.push(["Default branch:", r.defaultBranch || "(empty)"]);
                                    rows.push(["Language:", r.language || "-"]);
                                    rows.push(["Stars and forks:", r.stars + " stars, " + r.forks + " forks"]);
                                    rows.push(["Last pushed:", r.pushedAt ? window.ago(r.pushedAt) + " (" + new Date(r.pushedAt).toLocaleString(Qt.locale(), Locale.ShortFormat) + ")" : "never"]);
                                }
                                if (r.url)
                                    rows.push(["On GitHub:", r.url]);
                                rows.push(["On this computer:", r.path || "Not cloned"]);
                                if (r.path) {
                                    rows.push(["Branch:", s.branch + (s.upstream ? "  (tracks " + s.upstream + ")" : s.noCommits ? "  (no commits yet)" : "  (not on GitHub yet)")]);
                                    var st = [];
                                    if (s.files.length) st.push(window.plural(s.files.length, "changed file", "changed files"));
                                    if (s.ahead) st.push(window.plural(s.ahead, "commit", "commits") + " to push");
                                    if (s.behind) st.push(window.plural(s.behind, "commit", "commits") + " to pull");
                                    rows.push(["Status:", st.length ? st.join(", ") : "Up to date with GitHub (as of the last fetch)"]);
                                }
                                var cells = [];
                                rows.forEach(function (r) { cells.push(r[0], r[1]); });
                                return cells;
                            }
                            delegate: Text {
                                required property var modelData
                                required property int index
                                text: modelData
                                color: index % 2 ? Theme.text : Theme.shadow
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontSize
                            }
                        }
                    }
                    Row {
                        x: 14
                        anchors.bottom: parent.bottom
                        anchors.bottomMargin: 14
                        spacing: 8
                        Button95 {
                            visible: !!(window.current && !window.current.path && window.current.slug)
                            width: 110
                            text: "<u>C</u>lone..."
                            onClicked: cloneDialog.open(window.current.slug)
                        }
                        Button95 {
                            visible: !!(window.current && window.current.path && !window.current.slug) && window.signedIn
                            width: 140
                            text: "<u>P</u>ublish to GitHub..."
                            onClicked: newDialog.open(window.current.path)
                        }
                        Text {
                            visible: !!(window.current && window.current.path) && window.branches.length > 1
                            height: 21
                            verticalAlignment: Text.AlignVCenter
                            text: "Switch to branch:"
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSize
                        }
                        ComboBox95 {
                            visible: !!(window.current && window.current.path) && window.branches.length > 1
                            width: 180
                            popupMenu: popup
                            model: window.branches
                            currentIndex: Math.max(0, window.branches.indexOf(window.status.branch))
                            onActivated: (i) => window.switchBranch(window.branches[i])
                        }
                    }
                }

                // ------------------------------------------------- Changes
                Item {
                    Text {
                        visible: !(window.current && window.current.path)
                        anchors.centerIn: parent
                        text: "This repository isn't on this computer. Clone it to make changes."
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSize
                    }
                    Item {
                        visible: !!(window.current && window.current.path)
                        anchors.fill: parent
                        anchors.margins: 8

                        // the changed files, each with a box: in the commit or not
                        Item {
                            id: filesPane
                            width: 250
                            height: parent.height - commitBox.height - 6
                            CheckBox95 {
                                id: allBox
                                text: window.plural(window.status.files.length, "changed file", "changed files")
                                checked: window.status.files.length > 0 && window.commitPaths().length === window.status.files.length
                                onToggled: (on) => {
                                    var c = {};
                                    window.status.files.forEach(function (f) { c[f.path] = on; });
                                    window.checked = c;
                                }
                            }
                            Bevel {
                                y: 22
                                width: parent.width
                                height: parent.height - y
                                style: "sunken"
                                face: Theme.window
                                ListView {
                                    id: fileList
                                    x: 2
                                    y: 2
                                    width: parent.width - 4 - (fileScroll.visible ? fileScroll.width : 0)
                                    height: parent.height - 4
                                    clip: true
                                    model: window.status.files
                                    boundsBehavior: Flickable.StopAtBounds
                                    delegate: Item {
                                        id: fileRow
                                        required property var modelData
                                        width: fileList.width
                                        height: 20
                                        readonly property bool chosen: window.selectedFile === modelData.path
                                        Rectangle { anchors.fill: parent; color: fileRow.chosen ? Theme.selection : "transparent" }
                                        CheckBox95 {
                                            x: 3
                                            anchors.verticalCenter: parent.verticalCenter
                                            checked: window.checked[fileRow.modelData.path] !== false
                                            onToggled: (on) => {
                                                var c = Object.assign({}, window.checked);
                                                c[fileRow.modelData.path] = on;
                                                window.checked = c;
                                            }
                                        }
                                        Text {
                                            x: 24
                                            width: parent.width - x - 22
                                            anchors.verticalCenter: parent.verticalCenter
                                            text: fileRow.modelData.path
                                            elide: Text.ElideLeft
                                            color: fileRow.chosen ? Theme.selectionText : Theme.text
                                            font.family: Theme.fontFamily
                                            font.pixelSize: Theme.fontSize
                                        }
                                        Rectangle {
                                            anchors.right: parent.right
                                            anchors.rightMargin: 4
                                            anchors.verticalCenter: parent.verticalCenter
                                            width: 14
                                            height: 14
                                            color: ({ new: "#008000", deleted: "#b00000", renamed: "#0000a0", conflict: "#c06000" })[fileRow.modelData.kind] || "#806000"
                                            Text {
                                                anchors.centerIn: parent
                                                text: ({ new: "+", deleted: "-", renamed: "R", conflict: "!" })[fileRow.modelData.kind] || "M"
                                                color: "#ffffff"
                                                font.family: Theme.fontFamily
                                                font.pixelSize: 10
                                                font.bold: true
                                            }
                                        }
                                        MouseArea {
                                            anchors.fill: parent
                                            anchors.leftMargin: 20
                                            onClicked: window.showDiff(fileRow.modelData)
                                        }
                                    }
                                }
                                ScrollBar95 {
                                    id: fileScroll
                                    anchors.right: parent.right
                                    anchors.rightMargin: 2
                                    y: 2
                                    height: parent.height - 4
                                    flickable: fileList
                                    stepSize: 20
                                    visible: needed
                                }
                            }
                        }

                        TextView {
                            x: filesPane.width + 6
                            width: parent.width - x
                            height: parent.height
                            diff: true
                            text: window.fileDiff
                            placeholder: window.status.files.length ? "Click a file to see what changed in it."
                                                                    : "No changes: everything here is committed."
                        }

                        // the commit
                        Column {
                            id: commitBox
                            anchors.bottom: parent.bottom
                            width: filesPane.width
                            spacing: 4
                            TextField95 {
                                id: summaryField
                                width: parent.width
                                onAccepted: if (commitButton.enabled) window.commit()
                                Text {
                                    visible: !parent.text && !parent.activeFocus
                                    x: 6
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: "Summary (required)"
                                    color: Theme.shadow
                                    font.family: Theme.fontFamily
                                    font.pixelSize: Theme.fontSize
                                }
                            }
                            TextField95 {
                                id: descriptionField
                                width: parent.width
                                Text {
                                    visible: !parent.text && !parent.activeFocus
                                    x: 6
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: "Description"
                                    color: Theme.shadow
                                    font.family: Theme.fontFamily
                                    font.pixelSize: Theme.fontSize
                                }
                            }
                            Button95 {
                                id: commitButton
                                width: parent.width
                                text: "Co<u>m</u>mit to " + (window.status.branch || "the branch")
                                enabled: summaryField.text.trim().length > 0 && window.commitPaths().length > 0
                                onClicked: window.commit()
                            }
                        }
                    }
                }

                // ------------------------------------------------- History
                Item {
                    Text {
                        visible: !(window.current && window.current.path)
                        anchors.centerIn: parent
                        text: "This repository isn't on this computer. Clone it to see its history here, or View on GitHub."
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSize
                    }
                    Item {
                        visible: !!(window.current && window.current.path)
                        anchors.fill: parent
                        anchors.margins: 8
                        Bevel {
                            id: historyWell
                            width: parent.width
                            height: Math.round(parent.height * 0.45)
                            style: "sunken"
                            face: Theme.window
                            ListView {
                                id: historyList
                                x: 2
                                y: 2
                                width: parent.width - 4 - (historyScroll.visible ? historyScroll.width : 0)
                                height: parent.height - 4
                                clip: true
                                model: window.commits
                                boundsBehavior: Flickable.StopAtBounds
                                headerPositioning: ListView.OverlayHeader
                                readonly property int colWhen: 120
                                readonly property int colAuthor: 130
                                readonly property int colHash: 70
                                header: Row {
                                    z: 2
                                    ListHeader { width: historyList.width - historyList.colWhen - historyList.colAuthor - historyList.colHash; text: "Summary" }
                                    ListHeader { width: historyList.colAuthor; text: "Author" }
                                    ListHeader { width: historyList.colWhen; text: "When" }
                                    ListHeader { width: historyList.colHash; text: "Commit" }
                                }
                                delegate: Item {
                                    id: commitRow
                                    required property var modelData
                                    required property int index
                                    readonly property bool chosen: window.commitRow === index
                                    width: historyList.width
                                    height: 18
                                    Rectangle { anchors.fill: parent; color: commitRow.chosen ? Theme.selection : "transparent" }
                                    Row {
                                        Repeater {
                                            model: [[commitRow.modelData.subject, historyList.width - historyList.colWhen - historyList.colAuthor - historyList.colHash],
                                                    [commitRow.modelData.author, historyList.colAuthor],
                                                    [commitRow.modelData.when, historyList.colWhen],
                                                    [commitRow.modelData.short, historyList.colHash]]
                                            delegate: Text {
                                                required property var modelData
                                                width: modelData[1]
                                                height: 18
                                                leftPadding: 6
                                                verticalAlignment: Text.AlignVCenter
                                                text: modelData[0]
                                                elide: Text.ElideRight
                                                color: commitRow.chosen ? Theme.selectionText : Theme.text
                                                font.family: Theme.fontFamily
                                                font.pixelSize: Theme.fontSize
                                            }
                                        }
                                    }
                                    MouseArea { anchors.fill: parent; onClicked: window.showCommit(commitRow.index) }
                                }
                                Text {
                                    visible: historyList.count === 0
                                    anchors.centerIn: parent
                                    text: window.status.noCommits ? "No commits yet." : ""
                                    color: Theme.shadow
                                    font.family: Theme.fontFamily
                                    font.pixelSize: Theme.fontSize
                                }
                            }
                            ScrollBar95 {
                                id: historyScroll
                                anchors.right: parent.right
                                anchors.rightMargin: 2
                                y: 2
                                height: parent.height - 4
                                flickable: historyList
                                stepSize: 18
                                visible: needed
                            }
                        }
                        TextView {
                            y: historyWell.height + 6
                            width: parent.width
                            height: parent.height - y
                            diff: true
                            text: window.commitText
                            placeholder: "Click a commit to see what it changed."
                        }
                    }
                }

                // ---------------------------------- Issues, Pull Requests
                IssueList { kind: "issue"; entries: window.issues }
                IssueList { kind: "pr"; entries: window.pulls }
            }
        }
    }

    // the Issues and Pull Requests pages
    component IssueList: Item {
        id: page
        property string kind: "issue"
        property var entries: []
        readonly property bool available: !!(window.current && window.current.slug) && window.signedIn

        Text {
            visible: !page.available
            anchors.centerIn: parent
            text: !(window.current && window.current.slug) ? "This repository isn't on GitHub." : "Sign in to GitHub to see these."
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSize
        }
        Item {
            visible: page.available
            anchors.fill: parent
            anchors.margins: 8
            Row {
                id: issueBar
                spacing: 12
                height: 22
                RadioButton95 {
                    anchors.verticalCenter: parent.verticalCenter
                    text: "Open"
                    checked: window.issueState === "open"
                    onClicked: { window.issueState = "open"; window.loadIssues(); }
                }
                RadioButton95 {
                    anchors.verticalCenter: parent.verticalCenter
                    text: "Closed"
                    checked: window.issueState === "closed"
                    onClicked: { window.issueState = "closed"; window.loadIssues(); }
                }
                RadioButton95 {
                    anchors.verticalCenter: parent.verticalCenter
                    text: "All"
                    checked: window.issueState === "all"
                    onClicked: { window.issueState = "all"; window.loadIssues(); }
                }
            }
            Button95 {
                anchors.right: parent.right
                width: 120
                text: page.kind === "pr" ? "New Pull Request..." : "New Issue..."
                onClicked: Hub.openUrl(window.current.url + (page.kind === "pr" ? "/compare" : "/issues/new"))
            }
            Bevel {
                y: 28
                width: parent.width
                height: parent.height - y
                style: "sunken"
                face: Theme.window
                ListView {
                    id: issueList
                    x: 2
                    y: 2
                    width: parent.width - 4 - (issueScroll.visible ? issueScroll.width : 0)
                    height: parent.height - 4
                    clip: true
                    model: page.entries
                    boundsBehavior: Flickable.StopAtBounds
                    headerPositioning: ListView.OverlayHeader
                    header: Row {
                        z: 2
                        ListHeader { width: 60; text: "#" }
                        ListHeader { width: issueList.width - 60 - 130 - 120; text: "Title" }
                        ListHeader { width: 130; text: page.kind === "pr" ? "Branch" : "Author" }
                        ListHeader { width: 120; text: "Updated" }
                    }
                    delegate: Item {
                        id: issueRow
                        required property var modelData
                        width: issueList.width
                        height: 18
                        Row {
                            Repeater {
                                model: [["#" + issueRow.modelData.number, 60],
                                        [(issueRow.modelData.isDraft ? "[Draft] " : "") + issueRow.modelData.title
                                         + ((issueRow.modelData.labels || []).length ? "  [" + issueRow.modelData.labels.map(function (l) { return l.name; }).join(", ") + "]" : ""),
                                         issueList.width - 60 - 130 - 120],
                                        [page.kind === "pr" ? issueRow.modelData.headRefName : (issueRow.modelData.author ? issueRow.modelData.author.login : ""), 130],
                                        [window.ago(issueRow.modelData.updatedAt), 120]]
                                delegate: Text {
                                    required property var modelData
                                    width: modelData[1]
                                    height: 18
                                    leftPadding: 6
                                    verticalAlignment: Text.AlignVCenter
                                    text: modelData[0]
                                    elide: Text.ElideRight
                                    color: Theme.text
                                    font.family: Theme.fontFamily
                                    font.pixelSize: Theme.fontSize
                                }
                            }
                        }
                        MouseArea { anchors.fill: parent; onDoubleClicked: Hub.openUrl(issueRow.modelData.url) }
                    }
                    Text {
                        visible: issueList.count === 0
                        anchors.centerIn: parent
                        text: window.loadingIssues ? "Looking..." : page.kind === "pr" ? "No pull requests." : "No issues."
                        color: Theme.shadow
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSize
                    }
                }
                ScrollBar95 {
                    id: issueScroll
                    anchors.right: parent.right
                    anchors.rightMargin: 2
                    y: 2
                    height: parent.height - 4
                    flickable: issueList
                    stepSize: 18
                    visible: needed
                }
            }
        }
    }

    // -------------------------------------------------------- status bar
    Item {
        id: statusBar
        anchors.bottom: parent.bottom
        width: parent.width
        height: 22
        Bevel {
            x: 2
            y: 2
            width: parent.width - 180 - 6
            height: parent.height - 4
            style: "thinSunken"
            Text {
                x: 6
                width: parent.width - 12
                anchors.verticalCenter: parent.verticalCenter
                elide: Text.ElideRight
                text: window.message
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSize
            }
        }
        Bevel {
            x: parent.width - 180 - 2
            y: 2
            width: 180
            height: parent.height - 4
            style: "thinSunken"
            Text {
                x: 6
                width: parent.width - 12
                anchors.verticalCenter: parent.verticalCenter
                elide: Text.ElideRight
                text: Hub.busy > 0 ? "Working..." : window.signedIn ? "Signed in as " + window.login : "Not signed in"
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSize
            }
        }
    }

    // ----------------------------------------------------------- dialogs
    Dialog95 {
        id: cloneDialog
        title: "Clone Repository"
        dialogWidth: 420
        dialogHeight: 150
        function open(slug) {
            cloneWhat.text = slug || "";
            cloneWhere.text = window.cloneFolder;
            show();
            (slug ? cloneWhere : cloneWhat).forceActiveFocus();
        }
        function accept() {
            var what = cloneWhat.text.trim(), slug = Hub.slugOf(what) || what.replace(/\.git$/, "");
            if (!/^[A-Za-z0-9_.-]+\/[A-Za-z0-9_.-]+$/.test(slug)) {
                window.fail("Clone", "Give the repository as owner/name, or its GitHub address.", function () { cloneWhat.forceActiveFocus(); });
                return;
            }
            var folder = Hub.expandPath(cloneWhere.text);
            if (!Hub.isDir(folder)) {
                window.fail("Clone", "The folder " + cloneWhere.text + " doesn't exist.", function () { cloneWhere.forceActiveFocus(); });
                return;
            }
            close();
            window.cloneTo(slug, folder);
        }
        Text { x: 12; y: 14; height: 21; verticalAlignment: Text.AlignVCenter; text: "Repository:"; font.family: Theme.fontFamily; font.pixelSize: Theme.fontSize }
        TextField95 { id: cloneWhat; x: 90; y: 14; width: parent.width - x - 12; onAccepted: cloneDialog.accept() }
        Text { x: 12; y: 46; height: 21; verticalAlignment: Text.AlignVCenter; text: "Into folder:"; font.family: Theme.fontFamily; font.pixelSize: Theme.fontSize }
        TextField95 { id: cloneWhere; x: 90; y: 46; width: parent.width - x - 12 - 82; onAccepted: cloneDialog.accept() }
        Button95 {
            x: parent.width - 12 - width; y: 45; width: 76; text: "Browse..."
            onClicked: { folderPicker.target = cloneWhere; folderPicker.openAt(Hub.expandPath(cloneWhere.text) || Hub.homePath(), ""); }
        }
        Row {
            anchors.right: parent.right
            anchors.rightMargin: 12
            y: 84
            spacing: 6
            Button95 { text: "Clone"; isDefault: true; onClicked: cloneDialog.accept() }
            Button95 { text: "Cancel"; onClicked: cloneDialog.close() }
        }
    }

    Dialog95 {
        id: newDialog
        title: source ? "Publish to GitHub" : "New Repository"
        dialogWidth: 420
        dialogHeight: 220
        property string source: ""
        function open(path) {
            source = path || "";
            newName.text = path ? Hub.localRepo(path).name || "" : "";
            newDescription.text = "";
            newPrivate.checked = true;
            newPublic.checked = false;
            newReadme.checked = true;
            newClone.checked = true;
            show();
            newName.forceActiveFocus();
        }
        function accept() {
            var name = newName.text.trim();
            if (!/^[A-Za-z0-9_.-]+(\/[A-Za-z0-9_.-]+)?$/.test(name)) {
                window.fail(title, "A repository's name can have letters, digits, - _ and . (and owner/ in front, for one of your organisations).",
                            function () { newName.forceActiveFocus(); });
                return;
            }
            close();
            window.createRepo(name, newDescription.text.trim(), newPrivate.checked, newReadme.checked, newClone.checked, source);
        }
        Text { x: 12; y: 14; height: 21; verticalAlignment: Text.AlignVCenter; text: "Name:"; font.family: Theme.fontFamily; font.pixelSize: Theme.fontSize }
        TextField95 { id: newName; x: 90; y: 14; width: parent.width - x - 12; onAccepted: newDialog.accept() }
        Text { x: 12; y: 46; height: 21; verticalAlignment: Text.AlignVCenter; text: "Description:"; font.family: Theme.fontFamily; font.pixelSize: Theme.fontSize }
        TextField95 { id: newDescription; x: 90; y: 46; width: parent.width - x - 12; onAccepted: newDialog.accept() }
        Row {
            x: 90
            y: 80
            spacing: 16
            RadioButton95 { id: newPrivate; text: "Private"; checked: true; onClicked: { checked = true; newPublic.checked = false; } }
            RadioButton95 { id: newPublic; text: "Public"; onClicked: { checked = true; newPrivate.checked = false; } }
        }
        CheckBox95 {
            id: newReadme
            visible: !newDialog.source
            x: 90; y: 106
            text: "Start it with a README"
            onToggled: (on) => { checked = on; }
        }
        CheckBox95 {
            id: newClone
            visible: !newDialog.source
            x: 90; y: 128
            text: "Clone it into " + window.cloneFolder
            onToggled: (on) => { checked = on; }
        }
        Text {
            visible: !!newDialog.source
            x: 90; y: 108
            width: parent.width - x - 12
            wrapMode: Text.WordWrap
            text: "Makes the repository on GitHub, adds it as origin and pushes " + newDialog.source + " to it."
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSize
        }
        Row {
            anchors.right: parent.right
            anchors.rightMargin: 12
            y: 156
            spacing: 6
            Button95 { text: newDialog.source ? "Publish" : "Create"; isDefault: true; onClicked: newDialog.accept() }
            Button95 { text: "Cancel"; onClicked: newDialog.close() }
        }
    }

    Dialog95 {
        id: renameDialog
        title: "Rename"
        dialogWidth: 380
        dialogHeight: 120
        function open() {
            renameField.text = window.current.name;
            show();
            renameField.forceActiveFocus();
            renameField.input.selectAll();
        }
        function accept() {
            var name = renameField.text.trim();
            if (!/^[A-Za-z0-9_.-]+$/.test(name) || name === window.current.name)
                return;
            close();
            window.renameRepo(name);
        }
        Text { x: 12; y: 14; height: 21; verticalAlignment: Text.AlignVCenter; text: "New name:"; font.family: Theme.fontFamily; font.pixelSize: Theme.fontSize }
        TextField95 { id: renameField; x: 80; y: 14; width: parent.width - x - 12; onAccepted: renameDialog.accept() }
        Row {
            anchors.right: parent.right
            anchors.rightMargin: 12
            y: 52
            spacing: 6
            Button95 { text: "Rename"; isDefault: true; onClicked: renameDialog.accept() }
            Button95 { text: "Cancel"; onClicked: renameDialog.close() }
        }
    }

    Dialog95 {
        id: describeDialog
        title: "Edit Description"
        dialogWidth: 440
        dialogHeight: 120
        function open() {
            describeField.text = window.current.description;
            show();
            describeField.forceActiveFocus();
        }
        function accept() {
            close();
            window.editDescription(describeField.text.trim());
        }
        TextField95 { id: describeField; x: 12; y: 14; width: parent.width - 24; onAccepted: describeDialog.accept() }
        Row {
            anchors.right: parent.right
            anchors.rightMargin: 12
            y: 52
            spacing: 6
            Button95 { text: "OK"; isDefault: true; onClicked: describeDialog.accept() }
            Button95 { text: "Cancel"; onClicked: describeDialog.close() }
        }
    }

    // deleting: type its name, as GitHub asks
    Dialog95 {
        id: deleteDialog
        title: "Delete Repository"
        dialogWidth: 440
        dialogHeight: 190
        function open() {
            deleteField.text = "";
            show();
            deleteField.forceActiveFocus();
        }
        readonly property bool confirmed: !!window.current && deleteField.text.trim() === window.current.slug
        Text {
            x: 12; y: 12
            width: parent.width - 24
            wrapMode: Text.WordWrap
            textFormat: Text.StyledText
            text: window.current ? "This deletes <b>" + window.current.slug + "</b> from GitHub for good: its code, history, issues, "
                                   + "pull requests and wiki. It can't be undone."
                                   + (window.current.path ? " (The copy on this computer stays.)" : "")
                                   + "<br><br>To be sure, type its full name:" : ""
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSize
        }
        TextField95 { id: deleteField; x: 12; y: 92; width: parent.width - 24; onAccepted: if (deleteDialog.confirmed) { deleteDialog.close(); window.deleteRepo(); } }
        Row {
            anchors.right: parent.right
            anchors.rightMargin: 12
            y: 126
            spacing: 6
            Button95 {
                width: 120
                text: "Delete It"
                enabled: deleteDialog.confirmed
                onClicked: { deleteDialog.close(); window.deleteRepo(); }
            }
            Button95 { text: "Cancel"; isDefault: true; onClicked: deleteDialog.close() }
        }
    }

    Dialog95 {
        id: optionsDialog
        title: "Options"
        dialogWidth: 460
        dialogHeight: 220
        function open() {
            optClone.text = window.cloneFolder;
            optScan.text = window.scanFolders.join("; ");
            show();
            optClone.forceActiveFocus();
        }
        function accept() {
            Hub.setSetting("cloneFolder", optClone.text.trim() || Hub.homePath());
            Hub.setSetting("scanFolders", optScan.text.split(";").map(function (s) { return s.trim(); })
                                                    .filter(function (s) { return s.length; }).join("\n"));
            close();
            window.refresh();
        }
        Text { x: 12; y: 14; height: 21; verticalAlignment: Text.AlignVCenter; text: "Clone into:"; font.family: Theme.fontFamily; font.pixelSize: Theme.fontSize }
        TextField95 { id: optClone; x: 110; y: 14; width: parent.width - x - 12 - 82 }
        Button95 {
            x: parent.width - 12 - width; y: 13; width: 76; text: "Browse..."
            onClicked: { folderPicker.target = optClone; folderPicker.openAt(Hub.expandPath(optClone.text) || Hub.homePath(), ""); }
        }
        Text { x: 12; y: 48; height: 21; verticalAlignment: Text.AlignVCenter; text: "Look for clones in:"; font.family: Theme.fontFamily; font.pixelSize: Theme.fontSize }
        TextField95 { id: optScan; x: 110; y: 48; width: parent.width - x - 12 }
        Text {
            x: 110; y: 74
            width: parent.width - x - 12
            wrapMode: Text.WordWrap
            text: "Folders to look in for git repositories (three folders down), with ; between them. "
                  + "Ones you clone or add with File > Add Local Repository are always listed."
            color: Theme.shadow
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSize
        }
        Row {
            anchors.right: parent.right
            anchors.rightMargin: 12
            y: 150
            spacing: 6
            Button95 { text: "OK"; isDefault: true; onClicked: optionsDialog.accept() }
            Button95 { text: "Cancel"; onClicked: optionsDialog.close() }
        }
    }

    FileDialog95 {
        id: folderPicker
        mode: "folder"
        messageBox: msgBox
        popupMenu: popup
        property Item target
        onAccepted: (path) => { if (target) target.text = path; }
    }
    FileDialog95 {
        id: localDialog
        mode: "folder"
        messageBox: msgBox
        popupMenu: popup
        onAccepted: (path) => window.addLocalFolder(path)
    }

    Menu95 { id: popup }
    MessageBox95 { id: msgBox }
}
