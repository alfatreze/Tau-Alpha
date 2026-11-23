# Social preview and banner

`social-preview-1280x640.png` (GitHub's social preview, uploaded by hand in the repository settings: there is no API for it) and `tau-alpha-<version>-banner.png` (2000x640, for the top of a README), in the layout of Tau Gallery's pair but set in Space Grotesk (`third_party/font/`, OFL).

Made by `tools/make_social_images.py` from four real Pocket screenshots of the 0.7.5 player (400x360, Menu+Start): Now Playing (`20261123_005840`), METERS list dark (`20261118_205150`), Configure Winamp Bars (`20261118_205159`) and METERS list light (`20261118_205307`), from the Pocket's `Memories/Screenshots/`. The screenshots are not kept in the repository (the Now Playing one shows an album cover). To rebuild for a new release:

    python3 tools/make_social_images.py --version 0.7.6 --shots now_playing.png meters_dark.png configure.png meters_light.png --out docs/images

The words (title, subtitle, four bullets, footer) and the colours are constants at the top of the script.
