OpenAssault translations
========================

Every folder here is one language (HU = Hungarian). English is the original game text and needs no folder.

Making a new translation
------------------------
1. Start OpenAssaultLauncher.exe, type the code of your language (e.g. DE) under Translation and press
   "Create translation". (Without the launcher: openassault.exe --export-lang DE in a command prompt.)
   This writes lang\DE\ with the original English texts read from your Extreme Assault game data, opens the
   folder, and never overwrites an existing one.
2. Translate the texts (UTF-8, any text editor):
   - DATA\MENU\TEXT\GFX.CHR            menus
   - DATA\GLOBAL\TEXT\INGAME.CHR       in-game messages
   - DATA\LEVELnn\MISSIONS\MISSION.CHR briefings, mission goals and messages of the six areas
     Change only the text inside [n]{...}; keep the numbers and the braces. Entries you delete stay English.
     Lines starting with ; are comments. Mission ticker texts must stay below 128 characters.
   - PORT.TXT   the texts OpenAssault added: "English text = translation", translate the right side only.
                @name is the language name shown in the menu.
3. Choose the language in the launcher or in the game's Options > Language.
   PORT.TXT also holds the launcher's texts; it shows them after the language is chosen there.

The fonts are the game's own (code page 850 plus the Hungarian letters), so letters outside them show as '?'.
Speech and the labels drawn into the graphics (e.g. the HUD "SCORE") stay English.
Developers can check a translation with: py tools\langcheck.py DE
