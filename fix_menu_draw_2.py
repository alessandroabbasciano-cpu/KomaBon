import re
with open('lib/KomaBon_Apps/AppMainMenu.cpp', 'r') as f:
    text = f.read()

text = re.sub(r'for \(size_t i = 1; i < apps\.size\(\); i\+\+\) \{.*?App\* app = apps\[i\];',
              'int numApps = apps.size() + 1;\n        for (size_t i = 1; i < numApps; i++) {\n            App* app = apps[i - 1];',
              text, flags=re.DOTALL)

with open('lib/KomaBon_Apps/AppMainMenu.cpp', 'w') as f:
    f.write(text)
