import os, subprocess

DEV_ROOT = r"C:\Users\User\Documents\Project\xbox360-dev"
BIN = os.path.join(DEV_ROOT, "sdk-compiler")
XEDK = os.environ.get("XEDK", r"C:\Program Files (x86)\Microsoft Xbox 360 SDK")
if not os.path.exists(os.path.join(XEDK, "include", "xbox", "xtl.h")):
    XEDK = os.path.join(DEV_ROOT, "sdk-xdk", "XDK")

INC = os.path.join(XEDK, "include", "xbox")
LIB = os.path.join(XEDK, "lib", "xbox")
ATG = os.path.abspath("Common")

ENV = os.environ.copy()
ENV["PATH"] = BIN + ";" + ENV.get("PATH", "")

OUT = os.path.abspath("Release")
os.makedirs(os.path.join(OUT, "Media", "Fonts"), exist_ok=True)

def run_tool(exe, args):
    res = subprocess.run([os.path.join(BIN, exe)] + args, env=ENV)
    if res.returncode != 0:
        raise SystemExit(f"Failed: {exe}")

font_xpr = os.path.join(OUT, "Media", "Fonts", "font.xpr")
run_tool("Bundler.exe", [os.path.abspath("Media/Fonts/font.rdf"), "-o", font_xpr])

cflags = ["/nologo", "/c", "/O2", "/MT", "/EHsc-", "/GR-", "/GS-", "/Gy", "/W3", "/D", "NDEBUG", "/D", "_XBOX", "/D", "XBOX", f"/I{INC}", r"/IC:\Users\User\Documents\Project\xbox360-dev\sdk-xdk\XDK\TechPreview\Jul12Compiler\include\xbox", f"/I{ATG}"]
srcs = [
    "main.cpp",
    "god_engine.cpp",
    "archive_io.cpp",
    r"lzma\7zAlloc.c",
    r"lzma\7zArcIn.c",
    r"lzma\7zBuf.c",
    r"lzma\7zBuf2.c",
    r"lzma\7zCrc.c",
    r"lzma\7zCrcOpt.c",
    r"lzma\7zDec.c",

    r"lzma\7zStream.c",
    r"lzma\Alloc.c",
    r"lzma\Bcj2.c",
    r"lzma\Bra.c",
    r"lzma\Bra86.c",
    r"lzma\BraIA64.c",
    r"lzma\CpuArch.c",
    r"lzma\Delta.c",
    r"lzma\Lzma2Dec.c",
    r"lzma\LzmaDec.c",
    os.path.join(ATG, "AtgConsole.cpp"),
    os.path.join(ATG, "AtgDevice.cpp"),
    os.path.join(ATG, "AtgFont.cpp"),
    os.path.join(ATG, "AtgResource.cpp"),
    os.path.join(ATG, "AtgUtil.cpp"),
]
objs = []
for s in srcs:
    obj = os.path.join(OUT, os.path.splitext(os.path.basename(s))[0] + ".obj")
    objs.append(obj)
    run_tool("cl.exe", cflags + [f"/Fo{obj}", os.path.abspath(s)])

pe_out = os.path.join(OUT, "ISO2GOD.exe")
xex_out = os.path.join(OUT, "OmniConvert360.xex")
cfg_out = os.path.join(OUT, "xexconfig.xml")
libs = ["xapilib.lib", "d3d9.lib", "d3dx9.lib", "xgraphics.lib", "xboxkrnl.lib", "xaudio2.lib", r"C:\Users\User\Documents\Project\xbox360-dev\X-Store\Release\miniz.lib"]
run_tool("link.exe", ["/nologo", "/INCREMENTAL:NO", "/DEBUG:NONE", "/OPT:REF", "/OPT:ICF", "/ENTRY:mainCRTStartup", "/SUBSYSTEM:XBOX", f"/LIBPATH:{LIB}", r"/LIBPATH:C:\Users\User\Documents\Project\xbox360-dev\sdk-xdk\XDK\lib\xbox", f"/OUT:{pe_out}"] + objs + libs)

with open(cfg_out, "w", encoding="ascii") as f:
    f.write(f'<xex><titleid id="0x4F4D0001"/><section name="font" file="{font_xpr}" memory="RO"/></xex>\n')

run_tool("imagexex.exe", ["/nologo", f"/CONFIG:{cfg_out}", f"/IN:{os.path.join(OUT, 'ISO2GOD.pe')}", f"/OUT:{xex_out}"])
print("SUCCESS! Built", xex_out)









