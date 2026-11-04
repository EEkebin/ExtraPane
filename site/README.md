# ExtraPane showcase

Static GitHub Pages site at https://eekebin.github.io/ExtraPane/. No JavaScript or build dependencies are required. `index.html` contains the copy; `style.css` contains the responsive layout. The desktop composition is an illustration, not an application screenshot. Platform setup links point to the repository's maintained guides.

`assets/icon.png` is copied unchanged from `assets/branding/icon.png`. Keep both in sync when the application icon changes. The README banner embeds this same icon. Download links use GitHub's latest stable release redirect, so they do not need a version bump.

The Showcase site workflow deploys changes from main. GitHub repository Settings → Pages must use GitHub Actions as its source. Feature branches do not deploy. Local preview: `python -m http.server 8080 --directory site`.

Do not advertise unmerged capabilities, guaranteed latency, unverified hardware support, or a license the project has not selected. There is no donation section or analytics.
