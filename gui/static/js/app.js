(function () {
    "use strict";

    /* ── Clipboard ────────────────────────────────────────────────────── */
    function getElementText(id) {
        var el = document.getElementById(id);
        return el ? (el.innerText || el.textContent || "") : "";
    }

    function copyText(text) {
        if (!text) return Promise.resolve(false);
        if (navigator.clipboard && navigator.clipboard.writeText) {
            return navigator.clipboard.writeText(text).then(function () { return true; });
        }
        var ta = document.createElement("textarea");
        ta.value = text;
        ta.style.cssText = "position:absolute;left:-9999px";
        document.body.appendChild(ta);
        ta.select();
        try { document.execCommand("copy"); } finally { document.body.removeChild(ta); }
        return Promise.resolve(true);
    }

    function flashCopied(btn) {
        var orig = btn.textContent;
        btn.textContent = "Copied";
        btn.disabled = true;
        setTimeout(function () { btn.textContent = orig; btn.disabled = false; }, 1300);
    }

    function initCopyButtons() {
        document.querySelectorAll("[data-copy-target]").forEach(function (btn) {
            btn.addEventListener("click", function () {
                copyText(getElementText(btn.getAttribute("data-copy-target")))
                    .then(function (ok) { if (ok) flashCopied(btn); });
            });
        });
    }

    /* ── Active nav link ──────────────────────────────────────────────── */
    function initActiveNav() {
        var path = window.location.pathname;
        document.querySelectorAll(".nav-link[href]").forEach(function (link) {
            var href = link.getAttribute("href");
            if (href && href !== "/" && path.indexOf(href) === 0) {
                link.classList.add("active");
            } else if (href === "/" && path === "/") {
                link.classList.add("active");
            }
        });
    }

    /* ── Form loading states ──────────────────────────────────────────── */
    function setLoading(btn, label) {
        btn.disabled = true;
        btn.classList.add("loading");
        btn.setAttribute("data-orig", btn.textContent);
        btn.innerHTML = '<span class="spinner"></span>' + (label || "Running…");
    }

    function initFormLoading() {
        /* Agent run form */
        var agentForm = document.getElementById("agent-run-form");
        if (agentForm) {
            agentForm.addEventListener("submit", function (e) {
                var applyBox = document.getElementById("apply-to-original");
                if (applyBox && applyBox.checked) {
                    var ok = window.confirm(
                        "You selected ‘apply to original’ mode.\n" +
                        "The C engine will apply the patch outside the sandbox after checks pass.\n" +
                        "Continue?"
                    );
                    if (!ok) { e.preventDefault(); return; }
                }
                var btn = agentForm.querySelector("button[type='submit']");
                if (btn) setLoading(btn, "Running agent…");
            });
        }

        /* C2ASM run form */
        var c2asmForm = document.getElementById("c2asm-run-form");
        if (c2asmForm) {
            c2asmForm.addEventListener("submit", function () {
                var btn = c2asmForm.querySelector("button[type='submit']");
                if (btn) setLoading(btn, "Compiling…");
            });
        }
    }

    /* ── Flash dismiss ────────────────────────────────────────────────── */
    function initFlashDismiss() {
        document.querySelectorAll(".alert").forEach(function (alert) {
            var btn = document.createElement("button");
            btn.className = "alert-dismiss";
            btn.setAttribute("aria-label", "Dismiss");
            btn.textContent = "×";
            btn.addEventListener("click", function () {
                alert.style.opacity = "0";
                alert.style.transition = "opacity 200ms";
                setTimeout(function () { alert.remove(); }, 220);
            });
            alert.appendChild(btn);
        });
    }

    /* ── Textarea char counter ────────────────────────────────────────── */
    function initTextareaCounters() {
        document.querySelectorAll("textarea[maxlength]").forEach(function (ta) {
            var max = parseInt(ta.getAttribute("maxlength"), 10);
            var counter = document.createElement("div");
            counter.className = "char-counter";
            ta.parentNode.appendChild(counter);

            function update() {
                var len = ta.value.length;
                counter.textContent = len.toLocaleString() + " / " + max.toLocaleString() + " chars";
                counter.classList.toggle("over", len > max * 0.95);
            }

            ta.addEventListener("input", update);
            update();
        });
    }

    /* ── Request fill chips ───────────────────────────────────────────── */
    function initFillChips() {
        /* request_text textarea (project page) */
        var reqTa = document.querySelector("textarea[name='request_text']");
        if (reqTa) {
            document.querySelectorAll("[data-fill-request]").forEach(function (btn) {
                btn.addEventListener("click", function () {
                    reqTa.value = btn.getAttribute("data-fill-request") || "";
                    reqTa.focus();
                    reqTa.dispatchEvent(new Event("input"));
                    document.querySelectorAll("[data-fill-request]").forEach(function (b) {
                        b.classList.remove("active");
                    });
                    btn.classList.add("active");
                });
            });
        }

        /* source textarea (c2asm page) */
        var srcTa = document.querySelector("textarea[name='source']");
        if (srcTa) {
            document.querySelectorAll("[data-fill-source]").forEach(function (btn) {
                btn.addEventListener("click", function () {
                    srcTa.value = btn.getAttribute("data-fill-source") || "";
                    srcTa.focus();
                    srcTa.dispatchEvent(new Event("input"));
                    document.querySelectorAll("[data-fill-source]").forEach(function (b) {
                        b.classList.remove("active");
                    });
                    btn.classList.add("active");
                });
            });
        }
    }

    /* ── Tab panels ───────────────────────────────────────────────────── */
    function initTabs() {
        document.querySelectorAll("[role='tablist']").forEach(function (list) {
            var btns   = list.querySelectorAll("[role='tab']");
            var panels = [];
            btns.forEach(function (btn) {
                var panelId = btn.getAttribute("aria-controls");
                var panel = document.getElementById(panelId);
                if (panel) panels.push(panel);
            });

            function activate(targetBtn) {
                btns.forEach(function (b) {
                    b.setAttribute("aria-selected", "false");
                    b.classList.remove("active");
                });
                panels.forEach(function (p) { p.classList.remove("active"); });
                targetBtn.setAttribute("aria-selected", "true");
                targetBtn.classList.add("active");
                var panel = document.getElementById(targetBtn.getAttribute("aria-controls"));
                if (panel) panel.classList.add("active");
            }

            btns.forEach(function (btn) {
                btn.addEventListener("click", function () { activate(btn); });
            });

            /* activate pre-selected tab (set by Jinja), else first */
            var pre = list.querySelector("[aria-selected='true']");
            if (pre) activate(pre);
            else if (btns.length > 0) activate(btns[0]);
        });
    }

    /* ── Advanced options toggle ──────────────────────────────────────── */
    function initAdvancedToggle() {
        document.querySelectorAll(".adv-toggle").forEach(function (btn) {
            var targetId = btn.getAttribute("aria-controls");
            var body = document.getElementById(targetId);
            if (!body) return;

            btn.addEventListener("click", function () {
                var expanded = btn.getAttribute("aria-expanded") === "true";
                btn.setAttribute("aria-expanded", expanded ? "false" : "true");
                body.classList.toggle("open", !expanded);
            });
        });
    }

    /* ── Auto-open first accordion if it has content ─────────────────── */
    function initAutoOpenAccordion() {
        var first = document.querySelector(".accordion-stack .details-box");
        if (first) first.setAttribute("open", "");
    }

    /* ── Boot ─────────────────────────────────────────────────────────── */
    document.addEventListener("DOMContentLoaded", function () {
        initCopyButtons();
        initActiveNav();
        initFormLoading();
        initFlashDismiss();
        initTextareaCounters();
        initFillChips();
        initTabs();
        initAdvancedToggle();
        initAutoOpenAccordion();
    });
})();
