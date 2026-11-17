# Drive Mad local browser and Windows build

## Browser

From this directory, run:

```powershell
npm run dev
```

Open <http://localhost:1420/>. The game assets are served locally and do not load a Poki SDK. The Tauri bundle uses the clean `dist` copy of these browser assets.

Rewarded-ad requests use the local bridge in `webapp/source_min.js`: they notify the game that the request was shown and immediately notify it that the reward was earned.

## Windows/Tauri

Install Node.js, Rust (MSVC toolchain), and WebView2. Then run:

```powershell
npm install
npm run tauri dev
```

Create a Windows installer with:

```powershell
npm run tauri:build
```

`tauri:build` synchronizes the browser assets into `dist` before building.
