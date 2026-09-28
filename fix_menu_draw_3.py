import re
with open('lib/KomaBon_Apps/AppMainMenu.cpp', 'r') as f:
    text = f.read()

text = text.replace('int numAppItems = apps.size() - 1;', 'int numAppItems = apps.size();')
text = text.replace('int numAppItems = numApps - 1;', 'int numAppItems = numApps - 1;')

with open('lib/KomaBon_Apps/AppMainMenu.cpp', 'w') as f:
    f.write(text)
