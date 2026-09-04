# SoundRemote Server (tea4go Fork)

**[EN](README.en.md) · [简体中文](README.md)**

 A desktop application that pairs with Android devices through the [SoundRemote client](<https://github.com/SoundRemote/client-android>) to provide the following features:

 - Capture audio and send it to the client device.
- Simulate keyboard shortcuts received from the client. Some shortcuts, such as `Ctrl + Alt + Delete` and `Win + L`, are currently not supported.

![Main Window](https://github.com/user-attachments/assets/1b86c980-132d-4661-87ed-dbe3dd670a8a "Main Window")

 ## Fork Information

 This project is forked from [SoundRemote/server-windows](<https://github.com/SoundRemote/server-windows>), created by **Aleksandr Shipovskii**, Copyright © 2025, and continues to be released under the GPL-3.0 license.

 ### Major Changes (tea4go, 2026-07)

 - Chinese/English internationalization, switchable via the `Language` key in `settings.ini`
- Configurable fonts (`Font` / `Font.Size` / `Font.Bold`)
- Changed the client and keyboard shortcut sections to a tabbed layout, providing more usable space for text boxes
- Changed the client list to use a `ListBox`, with mouse-hover highlighting
- Filters out `0.0.0.0` and loopback addresses from the IP list and displays network adapter names
- Updated the update checker with HTTP timeouts, thread lifecycle protection, and 24-hour throttling
- Double-click the text box on the "Keyboard Shortcuts" tab to clear its history
- Standardized interactive elements to use the hand cursor
- Added a new `run_win.ps1` build script with the following options: `-Check` / `-InstallDeps` / `-Build` / `-Run` / `-Test` / `-Publish`

 ## Build

 ### Prerequisites

 - Visual Studio 2022 or later, with the **Desktop development with C++** workload installed
- vcpkg integrated with MSBuild in Visual Studio

 Using the PowerShell build script is recommended:

```powershell
.\run_win.ps1 -Check           # Check whether the environment is ready
.\run_win.ps1 -InstallDeps     # Install NuGet + vcpkg dependencies
.\run_win.ps1 -Build            # Build (Release/x64)
.\run_win.ps1 -Run              # Run the compiled executable
.\run_win.ps1 -Test             # Run unit tests
.\run_win.ps1 -Publish          # Publish to GitHub Release
.\run_win.ps1 -Publish -Target gitee  # Publish to Gitee Release
```

 ## License

 This project is licensed under the **GPL-3.0** license. The complete license text is available in the `COPYING` file. The third-party Opus dependency is licensed under a BSD-style license; see `opus_license.txt`.

 According to GPL-3.0, binary distributions must be accompanied by the complete source code or provide a written offer for obtaining the source code. The source code for this project is hosted at https://github.com/tea4go/SoundRemote-WinServer.