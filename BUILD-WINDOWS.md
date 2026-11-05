# Build the Windows VST3

This ZIP contains the source, not a compiled plug-in. It includes a Windows GitHub Actions build workflow.

1. Create a GitHub repository and upload the **contents** of this `CBVBSampler` folder, including `.github/workflows/build-windows-vst3.yml`.
2. Open the repository's **Actions** tab, select **Build CBVB Sampler Windows VST3**, and click **Run workflow**.
3. When the build succeeds, download the **CBVB-Sampler-Windows-VST3** artifact from the run's Artifacts section.
4. Unzip the artifact, then unzip the enclosed `CBVB-Sampler-Windows-VST3.zip`. Copy `CBVB Sampler.vst3` into `C:\Program Files\Common Files\VST3\`.

The workflow fetches JUCE 8.0.4 via CMake, compiles in Release mode with MSVC, and packages the VST3. Build success has not been verified in this environment.
