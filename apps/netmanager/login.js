// Network Manager's login filler, in the device's page: runs in a world of
// its own (WebEngineScript.ApplicationWorld), so the page's scripts can't
// see it or reach the program through it. When the program asks
// (fillRequested: only on the device's own pages), it fills the visible
// login form's user name and password, and submits it if told to. A login
// form drawn late by the page's scripts is waited for, for a few seconds.
(function () {
    "use strict";
    if (window.__w95device || typeof qt === "undefined" || !qt.webChannelTransport || typeof QWebChannel === "undefined")
        return;
    window.__w95device = true;

    function visible(el) {
        if (!el || el.disabled || el.readOnly)
            return false;
        var r = el.getBoundingClientRect();
        var style = window.getComputedStyle(el);
        return r.width > 4 && r.height > 4 && style.visibility !== "hidden" && style.display !== "none";
    }
    function passwordField() {
        var all = Array.prototype.filter.call(document.querySelectorAll("input[type=password]"), visible);
        return all.length ? all[0] : null;
    }
    // the text box before the password field (in its form): the user name
    function userFieldFor(pw) {
        var scope = pw.form || document;
        var inputs = Array.prototype.filter.call(scope.querySelectorAll("input"), function (i) {
            var t = (i.getAttribute("type") || "text").toLowerCase();
            return (t === "text" || t === "email" || t === "") && visible(i)
                && (i.compareDocumentPosition(pw) & Node.DOCUMENT_POSITION_FOLLOWING);
        });
        return inputs.length ? inputs[inputs.length - 1] : null;
    }
    // as typing would set it (the page's own code listens for input)
    function put(el, value) {
        el.focus();
        Object.getOwnPropertyDescriptor(HTMLInputElement.prototype, "value").set.call(el, value);
        el.dispatchEvent(new Event("input", { bubbles: true }));
        el.dispatchEvent(new Event("change", { bubbles: true }));
        el.dispatchEvent(new KeyboardEvent("keyup", { bubbles: true }));
    }
    function submit(pw) {
        var scope = pw.form || document;
        var button = scope.querySelector("button[type=submit], input[type=submit]")
            || Array.prototype.filter.call(scope.querySelectorAll("button, input[type=button], a"), function (b) {
                   return visible(b) && /log\s*-?\s*in|sign\s*-?\s*in|anmelden|connexion|entrar|ok/i.test(b.textContent || b.value || "");
               })[0];
        if (button)
            button.click();
        else if (pw.form && pw.form.requestSubmit)
            pw.form.requestSubmit();
        else
            pw.dispatchEvent(new KeyboardEvent("keydown", { key: "Enter", keyCode: 13, bubbles: true }));
    }

    new QWebChannel(qt.webChannelTransport, function (channel) {
        var device = channel.objects.w95device;
        if (!device)
            return;
        device.fillRequested.connect(function (username, password, andSubmit) {
            var tries = 0;
            (function attempt() {
                var pw = passwordField();
                if (!pw) {
                    if (++tries < 20)
                        setTimeout(attempt, 300);
                    else
                        device.result("noform");
                    return;
                }
                var user = userFieldFor(pw);
                if (user && username)
                    put(user, username);
                if (password)
                    put(pw, password);
                device.result(andSubmit ? "submitted" : "filled");
                if (andSubmit)
                    setTimeout(function () { submit(pw); }, 150);
            })();
        });
        device.ready();
    });
})();
