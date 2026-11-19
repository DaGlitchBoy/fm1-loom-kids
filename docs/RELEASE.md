# Prepare a LION KIDS browser release

The `Build LION KIDS emulator` GitHub Actions workflow builds and tests the pinned source. Its `lion-kids-release` artifact contains the browser ZIP, complete corresponding source with submodules, and SHA-256 checksums. Download the artifact from the run for the exact release commit. GitHub's automatic source archives omit submodule contents. Attach the corresponding-source archive with the browser ZIP.

## GitHub draft

1. Choose a public version and tag the reviewed commit. A tag such as `v1.0.0-rc1` builds the browser candidate. The workflow produces artifacts without publishing a release.
2. Open **Releases → Draft a new release** and select that tag. Title the draft **LION KIDS 1.0.0-rc1** and attach `lion-kids-web.zip`, `lion-kids-source.tar.gz`, and `SHA256SUMS`.
3. Paste the notes below. Review the draft and mark it as a prerelease if using an RC tag. Publish when ready.

### GitHub release notes

LION KIDS is an eight-track groovebox you can play in a desktop browser. It starts with a playable keyboard, eight distinct sounds, colored category icons, and three song slots you can name with pixel art.

- **Performance and Steps** alternate on SEQ and remember the last view. K4 browses steps on a track, and EDIT toggles the selected step in Steps.
- **Sound controls** stop at either end. Keys and Plucks start with reverb and delay.
- **Live recording and editing** include overdub, step velocity, root, length and swing, plus session undo and redo.
- **Song slots** support saving, opening and clearing with confirmation. Song data lives in your browser.

Download the browser ZIP and follow the included **LION KIDS user manual**. The source archive includes the complete corresponding source and license notices. LION KIDS builds on **LOOM-8 by Charles Vestal**. This is a desktop-browser emulator release.

## Community posts

Replace `<release URL>` with the actual GitHub release URL once it is public. The public post previews can include the HOME, Performance or Steps screenshot from the manual.

**M‑VAVE forum title**
LION KIDS – a browser groovebox based on LOOM-8

**M‑VAVE forum post**
I made LION KIDS to make the first few minutes of playing and recording a loop easier. It has eight playable tracks, a Performance view, a step editor, and three songs you can name with pixel art. It's based on LOOM-8 by Charles Vestal. This release runs in a desktop browser, with no FM‑1 connection required. The download and source are here: <release URL>. Feedback on the controls and the first-run experience is welcome.

**Reddit title**
LION KIDS is an eight-track browser groovebox for learning to make a loop

**Reddit post**
LION KIDS is a small open-source groovebox based on Charles Vestal's LOOM-8. You can play a preset, record a loop, switch between Performance and a step editor, and save three songs with pixel-art names. It comes with a browser ZIP, a quick-start manual and the source: <release URL>. I'd like to hear which controls felt intuitive and where you got stuck.

Before posting, check the community's rules for project announcements and self-promotion. Use the release link instead of an installer or firmware link.

## The Firmware Hub

The current deliverable is a browser emulator. The inherited device target builds LOOM-8 Studio. A LION KIDS listing for installation needs a separate device port, its own package identity and manifest, an applicable Hub-compatible build/provenance path, and hardware validation. The inherited firmware manifest and installer refer to upstream LOOM-8. See the [platform's Firmware Hub metadata documentation](../base/docs/FIRMWARE-JSON.md).

## Rename and privacy checklist

The GitHub repository name and URL can drop “loom” without renaming the emulator's internal build files. After a repository rename, update your clone's `origin`, any documentation links, and the URLs in announcements. Review existing Pages configuration and links, external indexes and badges, and reusable workflow calls or actions referenced by repository name. Existing web and Git links usually redirect until the old name is reused. GitHub Pages URLs and action references have separate redirect limitations. Changing the address used to run the emulator can change its IndexedDB origin and make older songs appear missing.

Review the GitHub account's public name, profile email, avatar and account handle used to publish. A release can show the publisher's public account and its profile details. Enable GitHub email privacy and use the GitHub-provided no-reply email for new commits. Inspect the final tag, generated release notes, screenshots and assets. The current branch histories have been cleaned of the earlier private commit email. Previously shared commit IDs can still resolve in GitHub's retained copies. GitHub Support controls removal of those copies.

- [GitHub repository rename behavior](https://docs.github.com/en/repositories/creating-and-managing-repositories/renaming-a-repository)
- [GitHub commit email settings](https://docs.github.com/en/account-and-profile/how-tos/email-preferences/setting-your-commit-email-address)
- [GitHub sensitive data removal](https://docs.github.com/en/authentication/keeping-your-account-and-data-secure/removing-sensitive-data-from-a-repository)
