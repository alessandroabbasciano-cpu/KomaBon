import re
with open('lib/KomaBon_Apps/AppMainMenu.cpp', 'r') as f:
    text = f.read()

# Replace apps list and loops in draw
text = re.sub(r'std::vector<App\*>& apps = appMgr\.getApps\(\);',
              'std::vector<App*> apps;\n    for(App* a : appMgr.getApps()) if(a->isVisibleInMenu()) apps.push_back(a);',
              text)

text = re.sub(r'int numApps = apps\.size\(\);', 'int numApps = apps.size() + 1;', text)
text = re.sub(r'for \(int i = 1; i < numApps; i\+\+\) \{.*?App\* app = apps\[i\];',
              'for (int i = 1; i < numApps; i++) {\n            App* app = apps[i - 1];',
              text, flags=re.DOTALL)

with open('lib/KomaBon_Apps/AppMainMenu.cpp', 'w') as f:
    f.write(text)
