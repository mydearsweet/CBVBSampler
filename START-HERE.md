# CBVB Sampler: fresh GitHub Windows VST3 build

This ZIP is an entire *source repository*, ready for a fresh GitHub upload. It includes:

* Full JUCE sampler source code, pastel GUI, and Fixed Velocity / Touch Sensitive toggle.
* MSVC-compatible `NcwDecoder.h` (`std::memcpy`, not `__builtin_memcpy`).
* `.github/workflows/build-windows-vst3.yml` for a Windows VST3 build on GitHub Actions.
* CMake configuration that downloads JUCE 8.0.4 at build time.

## Start fresh

1. On GitHub, create a **new empty repository** (do not initialize it with a README).
2. On your Windows computer, extract this ZIP and open the extracted folder.
3. In Windows File Explorer, enable **View > Show > Hidden items** to see `.github`.
4. Select **all items INSIDE** the extracted folder, including `.github`, `Source`, `CMakeLists.txt`, and this file. Drag them onto the GitHub repository's **Upload files** page. Do **not** upload the ZIP itself or add an extra enclosing folder.
5. Commit the uploaded files to the `main` branch. Verify the GitHub Code tab shows `CMakeLists.txt`, `Source`, and `.github` immediately at repository root.
6. Click **Actions > Build CBVB Sampler Windows VST3**. A run should start on push to `main`; otherwise use **Run workflow**.
7. When the run turns green, open its **Artifacts** and download `CBVB-Sampler-Windows-VST3`.
8. Unzip the artifact and its enclosed `CBVB-Sampler-Windows-VST3.zip`, then install the resulting `CBVB Sampler.vst3` into `C:\Program Files\Common Files\VST3\`.
9. In REAPER, run a VST re-scan.

The source is prepared but has **not been verified by a complete Windows/JUCE build** in this environment. If GitHub shows a red X, open the failed **Compile release VST3** step and copy the first compiler error plus a few surrounding lines.
