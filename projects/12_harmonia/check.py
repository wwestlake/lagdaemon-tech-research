import json  
with open('Characters/EditorMainFemale.gltf') as f:  
  d=json.load(f)  
  for a in d['accessors']:  
    if 'max' in a: print(a['max'])  
