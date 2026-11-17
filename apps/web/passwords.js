// Web Browser's saved passwords, in the page: runs in a world of its own
// (WebEngineScript.ApplicationWorld), so the page's scripts can't see it,
// call it or reach the browser through it. It sees the page's form fields
// as they are, and talks to the browser (the tab's "w95passwords" object,
// over Qt's web channel):
//   submitted(username, password)  a login is being sent: offer to save it
//   fill(username)                  a login field was clicked: the saved one?
//   loginReady(username, password)  (the browser's answer)
// Filling happens only when someone clicks or tabs into a visible login
// field, and the browser only answers with this site's own logins.
(function () {
    "use strict";
    if (window.__w95passwords || typeof qt === "undefined" || !qt.webChannelTransport)
        return;
    window.__w95passwords = true;

    function visible(el) {
        if (!el || el.disabled || el.readOnly)
            return false;
        var r = el.getBoundingClientRect();
        var style = window.getComputedStyle(el);
        return r.width > 4 && r.height > 4 && style.visibility !== "hidden" && style.display !== "none"
            && parseFloat(style.opacity || "1") > 0.1;
    }
    function passwordFields(scope) {
        return Array.prototype.filter.call((scope || document).querySelectorAll("input[type=password]"), visible);
    }
    // the text box before a password field (in its form): its user name
    function userFieldFor(pw) {
        var scope = pw.form || document;
        var inputs = Array.prototype.filter.call(scope.querySelectorAll("input"), function (i) {
            var t = (i.getAttribute("type") || "text").toLowerCase();
            return (t === "text" || t === "email" || t === "tel" || t === "") && visible(i)
                && (i.compareDocumentPosition(pw) & Node.DOCUMENT_POSITION_FOLLOWING);
        });
        return inputs.length ? inputs[inputs.length - 1] : null;
    }
    // set as typing would (the page's own code listens for input)
    function put(el, value) {
        Object.getOwnPropertyDescriptor(HTMLInputElement.prototype, "value").set.call(el, value);
        el.dispatchEvent(new Event("input", { bubbles: true }));
        el.dispatchEvent(new Event("change", { bubbles: true }));
    }

    new QWebChannel(qt.webChannelTransport, function (channel) {
        var browser = channel.objects.w95passwords;
        if (!browser)
            return;

        // ------------------------------------------------ being sent
        var lastSent = "";
        function capture(scope) {
            var pws = passwordFields(scope).filter(function (p) { return p.value; });
            if (!pws.length)
                return;
            var user = userFieldFor(pws[0]);
            var key = (user ? user.value : "") + "\n" + pws[0].value;
            if (key === lastSent)
                return;
            lastSent = key;
            browser.submitted(user ? user.value : "", pws[0].value);
        }
        document.addEventListener("submit", function (e) { capture(e.target); }, true);
        // (sites that log in from a script: a button clicked, Enter pressed)
        document.addEventListener("click", function (e) {
            if (!e.isTrusted || !e.target.closest)
                return;
            var b = e.target.closest("button, input[type=submit], input[type=button], [role=button]");
            if (b && passwordFields(b.form || document).length)
                setTimeout(function () { capture(b.form || document); }, 0);
        }, true);
        document.addEventListener("keydown", function (e) {
            if (e.isTrusted && e.key === "Enter" && e.target.type === "password")
                capture(e.target.form || document);
        }, true);

        // --------------------------------------------------- filling
        var waiting = null; // { user, pw }: the fields asked about
        browser.loginReady.connect(function (username, password) {
            var w = waiting;
            waiting = null;
            if (!w || !password || !w.pw.isConnected || w.pw.value)
                return;
            if (w.user && !w.user.value)
                put(w.user, username);
            put(w.pw, password);
        });
        // a person's click or Tab into a login field (not one a script
        // focused, nor a hidden one)
        var lastInput = 0;
        ["pointerdown", "keydown"].forEach(function (type) {
            document.addEventListener(type, function (e) { if (e.isTrusted) lastInput = Date.now(); }, true);
        });
        document.addEventListener("focusin", function (e) {
            var t = e.target;
            if (!(t instanceof HTMLInputElement) || !visible(t) || Date.now() - lastInput > 1000)
                return;
            var pw = t.type === "password" ? t : null;
            if (!pw) {
                var candidates = passwordFields(t.form || document);
                for (var i = 0; i < candidates.length && !pw; ++i)
                    if (userFieldFor(candidates[i]) === t)
                        pw = candidates[i];
            }
            if (!pw || pw.value)
                return;
            var user = userFieldFor(pw);
            waiting = { user: user, pw: pw };
            browser.fill(user ? user.value : "");
        }, true);
    });
})();
