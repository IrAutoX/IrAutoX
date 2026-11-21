import argparse
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument("--html",required=True);p.add_argument("--tailwind",required=True);p.add_argument("--output",required=True);a=p.parse_args()
h=Path(a.html).read_text(encoding="utf-8");c=Path(a.tailwind).read_text(encoding="utf-8")
h=h.replace("</head>","<style id=\"irx-tailwind\">"+c+"</style></head>",1)
d=")IRXAUTOX_HTML\""
if d in h: raise SystemExit("Reserved C++ raw string delimiter found")
o=Path(a.output);o.parent.mkdir(parents=True,exist_ok=True);o.write_text('#pragma once\ninline constexpr char kIrAutoXHtml[] = R"IRXAUTOX_HTML('+h+')IRXAUTOX_HTML";\n',encoding="utf-8")
