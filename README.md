Initial quick how-to:

What you need:
Visual studio 2013
3Dsmax 2015, with SDK installed (is separate install on DVD).

When the SDK is installed it's important to change a 'override' that autodesk
has in their template, so that we can compile with the visual studio 2013 tool-chain.

The SDK propertySheets overrides this setting, so no matter what you do in your 
project the template will still override it.

To change this - edit the version number in the file:
\maxsdk\ProjectSettings\propertySheets\3dsmax.general.project.settings.props
Edit this line:
<PlatformToolset>v110</PlatformToolset>
to:
<!--<PlatformToolset>v110</PlatformToolset>-->
That will make it not override the setting when we open our project later on.

First step is to follow the official Luxcore documentation to get that
compiled on windows.
The easiest way is to follow this guide:
http://www.luxrender.net/forum/viewtopic.php?f=22&t=10965&start=30#p105166

Once you have a working environment with all the Luxcore stuff compiled, then you add
the LuxMax_Interlan project to the Luxrays.sln.
Click Add->Existing project-> browse to LuxMaxInternal.vcxproj

It will most likely complain a bit about not finding libs and such, check the 
project setting and adjust the paths. This will be fixed with one single 
environment variable later on, for now you have to adjust manually.

For the shaders - you can just open the LuxMax_Internal_Shaders.sln file directly in a new
instance of Visual studio, they do not have any dependencies except for 3dsmax SDK.
Remember to compile the shaders in 'Release' mode, and not 'Debug'.

The projects compile the plugins straight into 3dsmax\plugins folder,
if you run windows 7\8 you might have to launch Visual studio as 'administrator' for it 
to be allowed to copy files there directly. Visual studio will fail during compilation if not.

For the plugin to find the luxcore functions and such, you need to copy the resulting DLL's from
the luxcore compilation and put those files in 3dsmax root.

typically you find these dll's here:
LuxProjectRoot\windows\Build_CMake\LuxRays\bin\Release\
Typically lux.dll is needed.

If you have questions or get stuck, then feel free to ask in the 'Luxmax' subforum.
But please make sure you have Luxcore stuff compiling before asking in 'Luxmax' forum.
For Luxcore compilation issues please ask in the 'Compilation & Portability' subforum.

When you have luxcore working - the rest is easy.