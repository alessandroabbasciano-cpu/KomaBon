import re
with open('lib/KomaBon_Core/BaseApp.h', 'r') as f:
    text = f.read()

text = text.replace('    virtual const char* getIcon() {\n    virtual bool isVisibleInMenu() { return true; }\n        return \"\";\n    }', '    virtual const char* getIcon() {\n        return \"\";\n    }\n    virtual bool isVisibleInMenu() { return true; }')

with open('lib/KomaBon_Core/BaseApp.h', 'w') as f:
    f.write(text)
