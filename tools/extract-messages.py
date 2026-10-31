#!/usr/bin/env python3
# Writes po/mkauroraenext_kwin_deco.pot from the strings of the .ui files (instead of KDE's extractrc + xgettext).
# The "comment" attribute of a <string> is its translation context, as for i18nc() / tr2i18n().
# Usage (in the project directory): tools/extract-messages.py, then update a translation with
#   msgmerge --update po/de/mkauroraenext_kwin_deco.po po/mkauroraenext_kwin_deco.pot
import glob, xml.etree.ElementTree as ET, datetime

def quote(s):
    return '"' + s.replace('\\', '\\\\').replace('"', '\\"').replace('\n', '\\n') + '"'

entries = {}   # (context, text) -> list of references
for ui in sorted(glob.glob("src/**/*.ui", recursive=True)):
    for e in ET.parse(ui).getroot().iter("string"):
        if e.text and e.get("notr") != "true":
            entries.setdefault((e.get("comment"), e.text), []).append(ui)

out = ['# Translation template of the mkAuroraeNext window decoration.',
       'msgid ""', 'msgstr ""',
       '"Project-Id-Version: mkauroraenext\\n"',
       f'"POT-Creation-Date: {datetime.datetime.now().strftime("%Y-%m-%d %H:%M%z")}\\n"',
       '"MIME-Version: 1.0\\n"', '"Content-Type: text/plain; charset=UTF-8\\n"', '"Content-Transfer-Encoding: 8bit\\n"', '']
for (context, text), refs in entries.items():
    out.append("#: " + " ".join(sorted(set(refs))))
    if context:
        out.append("msgctxt " + quote(context))
    out += ["msgid " + quote(text), 'msgstr ""', ""]
open("po/mkauroraenext_kwin_deco.pot", "w", encoding="utf-8").write("\n".join(out))
print(f"{len(entries)} Texte nach po/mkauroraenext_kwin_deco.pot")
