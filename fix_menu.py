import re
with open('lib/KomaBon_Apps/AppMainMenu.cpp', 'r') as f:
    text = f.read()

# Replace the select block
text = re.sub(r'\} else if \(action == INPUT_SELECT\) \{.*?\n\s*\}\n\}',
              '} else if (action == INPUT_SELECT) {\n        if (selectedIndex == 0 && _hasResume) {\n            AppReader* reader = static_cast<AppReader*>(appMgr.getAppByName(\"Reader\"));\n            if (reader) reader->resumeSavedBookOnStart();\n            ProgressStore::getInstance().setResumeOnBoot(true);\n            appMgr.switchTo(\"Reader\");\n        } else if (selectedIndex > 0 && selectedIndex <= (int)apps.size()) {\n            appMgr.switchTo(apps[selectedIndex - 1]->getName());\n        }\n    }\n}',
              text, flags=re.DOTALL)

with open('lib/KomaBon_Apps/AppMainMenu.cpp', 'w') as f:
    f.write(text)
