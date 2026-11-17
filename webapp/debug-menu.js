(function () {
  "use strict";

  window.createDebugMenu = function createDebugMenu() {
    if (window.__driveMadDebugMenu) {
      return;
    }

    var channel = typeof BroadcastChannel === "function"
      ? new BroadcastChannel("drive-mad-debug")
      : null;
    var paused = false;
    var muted = false;
    var panel = document.createElement("section");
    var status = document.createElement("p");
    var pauseButton;
    var audioButton;

    panel.setAttribute("aria-label", "Debug menu");
    panel.hidden = true;
    panel.style.cssText = "position:fixed;z-index:10001;top:48px;left:12px;width:280px;padding:12px;background:#0c121c;color:#fff;border:1px solid #5cc8ff;border-radius:6px;box-shadow:0 4px 16px #0008;font:14px Arial,sans-serif";

    var title = document.createElement("h2");
    title.textContent = "Drive Mad debug";
    title.style.cssText = "margin:0 0 8px;font-size:16px";
    panel.appendChild(title);

    status.style.cssText = "margin:0 0 8px;color:#b9d8e8;line-height:1.4";
    panel.appendChild(status);

    function setStatus(text) {
      status.textContent = text;
      if (channel) {
        channel.postMessage({ type: "status", text: text });
      }
    }

    function callRuntime(name, returnType, argTypes, args) {
      if (typeof postRunDone === "undefined" || !postRunDone) {
        setStatus("Runtime is still loading.");
        return false;
      }
      try {
        Module.ccall(name, returnType || "v", argTypes || [], args || []);
        return true;
      } catch (error) {
        setStatus("Failed: " + name + " (" + error.message + ")");
        return false;
      }
    }

    function resetSave() {
      Object.keys(localStorage).filter(function (key) {
        return key.indexOf("com.martinmagni.drivemad/") === 0;
      }).forEach(function (key) {
        localStorage.removeItem(key);
      });
      window.location.reload();
    }

    function execute(type) {
      if (type === "pause-toggle") {
        if (!gameStarted) {
          setStatus("Game has not started yet.");
          return;
        }
        paused = !paused;
        if (callRuntime(paused ? "app_pause" : "app_resume")) {
          pauseButton.textContent = paused ? "Resume game" : "Pause game";
          setStatus("Game: " + (paused ? "paused" : "running"));
        }
      } else if (type === "reward") {
        if (callRuntime("ad_rewarded_on_showed", "v", ["number"], [1])) {
          callRuntime("ad_rewarded_on_rewarded");
          setStatus("Reward granted.");
        }
      } else if (type === "interstitial") {
        if (callRuntime("ad_interstitial_on_showed", "v", ["number"], [1])) {
          setStatus("Interstitial ad event sent.");
        }
      } else if (type === "audio-toggle") {
        muted = !muted;
        if (muted) {
          Audio.pause();
        } else {
          Audio.resume();
        }
        audioButton.textContent = muted ? "Unmute audio" : "Mute audio";
        setStatus("Audio: " + (muted ? "muted" : "enabled"));
      } else if (type === "fullscreen") {
        try {
          if (document.fullscreenElement) {
            document.exitFullscreen();
          } else {
            document.getElementById("canvas").requestFullscreen();
          }
          setStatus("Fullscreen toggled.");
        } catch (error) {
          setStatus("Fullscreen unavailable: " + error.message);
        }
      } else if (type === "reset") {
        resetSave();
      } else if (type === "resize") {
        resizeCanvas(true);
        setStatus("Canvas size refreshed.");
      } else if (type === "reload") {
        window.location.reload();
      } else if (type === "status-request") {
        setStatus("Game: " + (gameStarted ? "started" : "waiting") + " | Runtime: " + (postRunDone ? "ready" : "loading"));
      }
    }

    function addAction(label, type) {
      var button = document.createElement("button");
      button.type = "button";
      button.textContent = label;
      button.style.cssText = "box-sizing:border-box;width:100%;margin-top:6px;padding:7px;border:0;border-radius:4px;background:#168dcc;color:#fff;cursor:pointer";
      button.addEventListener("click", function () {
        execute(type);
      });
      panel.appendChild(button);
      return button;
    }

    pauseButton = addAction("Pause game", "pause-toggle");
    addAction("Grant reward", "reward");
    addAction("Show interstitial event", "interstitial");
    audioButton = addAction("Mute audio", "audio-toggle");
    addAction("Toggle fullscreen", "fullscreen");
    addAction("Refresh canvas size", "resize");
    addAction("Reset local save", "reset");
    addAction("Reload game", "reload");
    var closeButton = addAction("Close", "close");
    closeButton.style.background = "#415466";
    closeButton.addEventListener("click", function () {
      panel.hidden = true;
    });

    if (channel) {
      channel.onmessage = function (event) {
        var data = event.data || {};
        if (data.type === "status-request" || data.type === "pause-toggle" || data.type === "reward" ||
            data.type === "interstitial" || data.type === "audio-toggle" || data.type === "fullscreen" ||
            data.type === "reset" || data.type === "resize" || data.type === "reload") {
          execute(data.type);
        }
      };
    }

    var debugButton = document.createElement("button");
    debugButton.type = "button";
    debugButton.textContent = "DEBUG";
    debugButton.title = "Open debug controls";
    debugButton.setAttribute("aria-expanded", "false");
    debugButton.style.cssText = "position:fixed;z-index:10001;top:12px;left:12px;padding:8px 12px;background:#168dcc;color:#fff;border:1px solid #5cc8ff;border-radius:4px;font:bold 12px Arial;cursor:pointer";
    debugButton.addEventListener("click", function () {
      panel.hidden = !panel.hidden;
      debugButton.setAttribute("aria-expanded", String(!panel.hidden));
      if (!panel.hidden) {
        execute("status-request");
      }
    });

    document.body.appendChild(debugButton);
    document.body.appendChild(panel);
    window.__driveMadDebugMenu = panel;
    execute("status-request");

    window.addEventListener("keydown", function (event) {
      if (event.key === "F1" || event.code === "F1" || event.key === "`" || event.code === "Backquote") {
        event.preventDefault();
        event.stopPropagation();
        panel.hidden = false;
        debugButton.setAttribute("aria-expanded", "true");
        execute("status-request");
      } else if (event.key === "Escape" && !panel.hidden) {
        panel.hidden = true;
        debugButton.setAttribute("aria-expanded", "false");
      }
    }, true);
  };
}());
