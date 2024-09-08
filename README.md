# Unreal-Engine
 Unreal Engine Version 4.26, Modified for Fortnite
The steps below will take you through cloning your own private fork, then compiling and running the editor yourself:

# Windows

1. Install GitHub for Windows then fork and clone our repository. To use Git from the command line, see the Setting up Git and Fork a Repo articles.

If you'd prefer not to use Git, you can get the source with the 'Download ZIP' button on the right. The built-in Windows zip utility will mark the contents of zip files downloaded from the Internet as unsafe to execute, so right-click the zip file and select 'Properties...' and 'Unblock' before decompressing it. Third-party zip utilities don't normally do this.

2. Install Visual Studio 2017. All desktop editions of Visual Studio 2017 can build UE4, including Visual Studio Community 2017, which is free for small teams and individual developers. To install the correct components for UE4 development, check the "Game Development with C++" workload, and the "Unreal Engine Installer" and "Nuget Package Manager" optional components.

3. Open your source folder in Explorer and run Setup.bat. This will download binary content for the engine, as well as installing prerequisites and setting up Unreal file associations. On Windows 8, a warning from SmartScreen may appear. Click "More info", then "Run anyway" to continue.

A clean download of the engine binaries is currently 3-4gb, which may take some time to complete. Subsequent checkouts only require incremental downloads and will be much quicker.

After that extract the "engine.zip" file from releases, and make sure to replace any files if it asks

4. Run GenerateProjectFiles.bat to create project files for the engine. It should take less than a minute to complete.

5. Load the project into Visual Studio by double-clicking on the UE4.sln file. Set your solution configuration to Development Editor and your solution platform to Win64, then right click on the UE4 target and select Build. It may take anywhere between 10 and 40 minutes to finish compiling, depending on your system specs.

6. After compiling finishes, you can load the editor from Visual Studio by setting your startup project to UE4 and pressing F5 to debug.
