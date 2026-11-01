# Software catalog and folders

`programs/catalog.json` is the authoritative editable catalog. Legacy `drp.js`, `soft.js` and custom JSON are read as literal data on first import; original scripts, targeting expressions, advertising arguments and silent flags are not executed. Removing a record does not cause it to reappear from the legacy sources.

Use **Software → Add a program** or the pencil beside a program to edit metadata. A local installer must be placed in `programs/` or a supported category folder. Supply its filename, publisher, category, description, optional publisher download URL and icon path. Icons are resolved from a catalog path, embedded known-program artwork, then a local installer's Windows icon; a neutral symbol is used if no artwork is available.

| Content | Folder |
| --- | --- |
| Browsers / network applications | `Internet` |
| Remote assistance, including TeamViewer | `Support` |
| Antivirus / security | `Security` |
| Players and codecs, including K-Lite | `Media` |
| Image viewers and utilities, including FastStone | `Tools` |
| Documents and office applications | `Office` |
| Messaging | `Communication` |
| Backup applications | `Backup` |
| Runtime packages | `System` |
| Uncategorized / older variants | `Other` / `Legacy` |

Known legacy category mistakes are corrected while reading records. Installer variants are chosen using catalog identity, detected Windows/architecture and available local files; arbitrary filename suffixes do not authorize installation. Older Windows needs a compatible application version. Ordinary setups run interactively after explicit selection, without legacy bundle arguments. Adding a security application makes it a catalog option; it does not create a new malware classifier.

Deleting a catalog entry and deleting its installer are separate actions. Installer deletion defaults off; a shared installer is protected.
