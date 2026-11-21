"""Native rendering/protocol checks with real U8g2. Requires PlatformIO deps,
and either g++/gcc or `pip install ziglang`. Optional Pillow for preview sheets.
Run from repository root: python tools/test_companion.py
"""
import importlib.util
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

root = Path(__file__).resolve().parent.parent
build = root / 'offline/.pio/host-qa'
build.mkdir(parents=True, exist_ok=True)
env = os.environ.get('LUCY_TEST_ENV', 'lucy_c3_mini')
libs = root / 'offline/.pio/libdeps' / env
clib = libs / 'U8g2/src/clib'
assert clib.exists(), f'Run pio run -e {env} in offline/ first'
if importlib.util.find_spec('ziglang'):
    cc = [sys.executable, '-m', 'ziglang', 'cc']
    cxx = [sys.executable, '-m', 'ziglang', 'c++']
else:
    cc, cxx = ['gcc'], ['g++']

def run(args):
    result = subprocess.run(args, cwd=build, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if result.returncode:
        print(result.stdout)
        raise SystemExit(result.returncode)

# Extract just the two real production fonts, preserving their declarations.
all_fonts = (clib / 'u8g2_fonts.c').read_text(encoding='latin-1')
fonts = ['#include "u8g2.h"\n']
for name in ('u8g2_font_helvR08_tf', 'u8g2_font_helvB08_tf'):
    fonts.append(re.search(r'const uint8_t '+name+r'\[.*?;\n', all_fonts, re.S).group())
(build / 'fonts.c').write_text('\n'.join(fonts), encoding='utf-8')
sources = [clib / (x+'.c') for x in (
    'u8g2_setup','u8g2_hvline','u8g2_ll_hvline','u8g2_line','u8g2_box',
    'u8g2_circle','u8g2_polygon','u8g2_font','u8g2_intersection','u8g2_kerning',
    'u8x8_8x8','u8x8_debounce',
    'u8x8_string','u8x8_setup','u8x8_cad','u8x8_byte','u8x8_gpio','u8x8_display','u8x8_message')]
sources.append(build / 'fonts.c')
objects = []
for source in sources:
    obj = build / (source.stem+'.o')
    run(cc+['-O2','-ffunction-sections','-fdata-sections','-I'+str(clib),'-c',str(source),'-o',str(obj)])
    objects.append(str(obj))
exe = build / ('companion-tests.exe' if os.name=='nt' else 'companion-tests')
run(cxx+['-std=c++17','-O1','-g','-UNDEBUG','-I'+str(clib),
         '-I'+str(root/'offline/src'),'-I'+str(libs/'ArduinoJson/src'),
         str(root/'tools/test_companion.cpp')]+objects+['-o',str(exe)])
subprocess.run([str(exe)],cwd=build,check=True)
try:
    from PIL import Image, ImageDraw
    paths = [build/'boot.pbm',build/'home.pbm',build/'settings-0.pbm',build/'settings-3.pbm',build/'settings-6.pbm']
    paths += sorted(build.glob('cat-*.pbm'))
    sheet = Image.new('RGB',(5*280, ((len(paths)+4)//5)*170),'#10151b')
    draw = ImageDraw.Draw(sheet)
    for i,path in enumerate(paths):
        frame = Image.open(path).convert('RGB').resize((256,128),Image.Resampling.NEAREST)
        x,y = (i%5)*280+12,(i//5)*170+10
        sheet.paste(frame,(x,y))
        draw.text((x,y+134),path.stem,fill='white')
    sheet.save(build/'preview.png')
    frames = [Image.open(p).convert('RGB').resize((512,256),Image.Resampling.NEAREST)
              for p in sorted(build.glob('motion-*.pbm'))]
    frames[0].save(build/'lucy-preview.gif',save_all=True,append_images=frames[1:],duration=120,loop=0)
    print('Preview:',build/'preview.png')
except ImportError:
    pass
