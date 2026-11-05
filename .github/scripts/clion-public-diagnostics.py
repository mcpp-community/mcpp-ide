"""Publish allowlisted classifications, never IDE logs or private file paths."""
from pathlib import Path
import collections,json,os,re,subprocess,xml.etree.ElementTree as ET

markers=['No usable sandbox','Failed to move to new namespace','Operation not permitted',
         'AppArmor','error while loading shared libraries','GPU process exited unexpectedly',
         'Failed to create browser','JCEF has been restarted','Failed to initialize JCEF',
         'CefApp initialization failed','JCEF is not supported','AWT-EventQueue']
counts=collections.Counter();classes=collections.Counter();missing=set();files=0
roots=[Path('out/ide-tests/tests'),Path('mide-plugins/clion/integration-tests/build/test-results/test')]
for root in roots:
    if not root.is_dir():continue
    for p in root.rglob('*'):
        if not p.is_file() or p.is_symlink() or p.suffix not in ['.log','.txt','.xml']:continue
        if p.stat().st_size>32*1024*1024:continue
        text=p.read_text(errors='replace');files+=1
        for marker in markers:counts[marker]+=text.count(marker)
        for name in re.findall(r'\b((?:com\.intellij|com\.jetbrains|org\.cef|java|javax|kotlin|io\.github\.mcppcommunity)\.[\w.$]*(?:Exception|Error))\b',text):classes[name]+=1
        missing.update(re.findall(r'error while loading shared libraries: ([A-Za-z0-9_.+-]+)',text))
tests=[]
for p in roots[1].glob('TEST-*.xml'):
    for test in ET.parse(p).getroot().findall('testcase'):
        tests.append(dict(classname=test.get('classname'),name=test.get('name'),failed=test.find('failure') is not None))
sysctl={}
for key in ['apparmor_restrict_unprivileged_userns','unprivileged_userns_clone']:
    p=Path('/proc/sys/kernel')/key
    sysctl[key]=p.read_text().strip() if p.is_file() else None
probe=subprocess.run(['unshare','-Ur','true'],capture_output=True,text=True)
report=dict(log_files_classified=files,markers=dict(counts),exception_classes=dict(classes),missing_libraries=sorted(missing),tests=tests,
            sysctl=sysctl,user_namespace_probe_exit=probe.returncode,user_namespace_probe_permission_denied='Operation not permitted' in probe.stderr,
            scope='Synthetic CI IDE contexts; only fixed diagnostic markers, public exception classes and test names. No raw logs, messages or paths.')
out=Path(os.environ['RUNNER_TEMP'])/'clion-public-diagnostics.json';out.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
