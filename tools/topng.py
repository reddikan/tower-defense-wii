import sys, glob, os
from PIL import Image

import re
src = open(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'source', 'game.c')).read()
m = re.search(r'game_palette\[32\]\[3\] = \{(.*?)\};', src, re.S)
nums = [int(x) for x in re.findall(r'\d+', m.group(1))]
pal = [tuple(nums[i:i+3]) for i in range(0, len(nums), 3)]
assert len(pal) == 32
flat = [c for p in pal for c in p] + [0]*(256-32)*3
out = sys.argv[1]
scale = int(sys.argv[2]) if len(sys.argv) > 2 else 3
for f in sorted(glob.glob(out + '/*.bin')):
    data = open(f, 'rb').read()
    im = Image.frombytes('P', (320, 180), data)
    im.putpalette(flat)
    im = im.convert('RGB').resize((320*scale, 180*scale), Image.NEAREST)
    im.save(f[:-4] + '.png')
    print(os.path.basename(f))
