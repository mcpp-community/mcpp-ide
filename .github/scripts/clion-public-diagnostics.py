"""Publish allowlisted classifications, never IDE logs or private file paths."""
from pathlib import Path
import collections,json,os,re,subprocess,xml.etree.ElementTree as ET

def public_frames(text):
    lines=text.splitlines()
    result=[]
    for index,line in enumerate(lines):
        kinds=[name for name in ['ExecutionFinishedException','UnhandledException'] if name in line]
        if not kinds:continue
        frames=[]
        for following in lines[index+1:index+65]:
            frame=re.search(r'\bat ((?:com\.intellij|com\.jetbrains|io\.github\.mcppcommunity|kotlinx\.coroutines|java\.util)\.[A-Za-z0-9_.$<>]+)\(([A-Za-z0-9_.$-]+\.(?:kt|java)):(\d+)\)',following)
            if frame:frames.append(dict(method=frame[1],source=frame[2],line=int(frame[3])))
        if frames:result.append(dict(kinds=kinds,frames=frames))
    return result

markers=['No usable sandbox','Failed to move to new namespace','Operation not permitted',
         'AppArmor','error while loading shared libraries','GPU process exited unexpectedly',
         'Failed to create browser','JCEF has been restarted','Failed to initialize JCEF',
         'CefApp initialization failed','JCEF is not supported','AWT-EventQueue']
counts=collections.Counter();classes=collections.Counter();missing=set();files=0;traces={}
contexts=os.environ.get('MCPP_DIAG_CONTEXTS')
ide_roots=[Path(p) for p in json.loads(Path(contexts).read_text())] if contexts else [Path('out/ide-tests/tests')]
test_root=Path('mide-plugins/clion/integration-tests/build/test-results/test')
roots=[*ide_roots,test_root]
for root in roots:
    if not root.is_dir():continue
    for p in root.rglob('*'):
        if not p.is_file() or p.is_symlink() or p.suffix not in ['.log','.txt','.xml']:continue
        if p.stat().st_size>32*1024*1024:continue
        text=p.read_text(errors='replace');files+=1
        for marker in markers:counts[marker]+=text.count(marker)
        for name in re.findall(r'\b((?:com\.intellij|com\.jetbrains|org\.cef|java|javax|kotlin|io\.github\.mcppcommunity)\.[\w.$]*(?:Exception|Error))\b',text):classes[name]+=1
        missing.update(re.findall(r'error while loading shared libraries: ([A-Za-z0-9_.+-]+)',text))
        for trace in public_frames(text):
            key=json.dumps(trace,sort_keys=True)
            if len(traces)<16 or key in traces:traces[key]=trace
tests=[]
allowed_tests={('PackagedPluginTest','packagedPluginStartsItsBundledBackend()'),
               ('SettingsInteractionTest','settingsValidateBeforeSavingInTheRealDialog()'),
               ('GraphInteractionTest','graphRendersNavigatesAndRecoversInJcef()'),
               ('GraphInteractionTest','graphExplainsUnavailableJcef()')}
other_tests=0
for p in test_root.glob('TEST-*.xml'):
    for test in ET.parse(p).getroot().findall('testcase'):
        if (test.get('classname'),test.get('name')) not in allowed_tests:
            other_tests+=1
            continue
        failure=test.find('failure')
        text=failure.text or '' if failure is not None else ''
        lines=[int(v) for v in re.findall(r'GraphInteractionTest\.kt:(\d+)',text)]
        source_lines={name:sorted({int(v) for v in re.findall(name+r'\.kt:(\d+)',text)})
                      for name in ['PackagedPluginTest','SettingsInteractionTest','GraphInteractionTest']}
        failure_markers={name:text.count(name) for name in ['IDE failure:','ExecutionFinishedException',
                         'reportTestFailure','ErrorReporterToCI','the real debug action finishes in the IDE',
                         'the native debugger did not suspend at fixture line','the native variables view shows answer']}
        tests.append(dict(classname=test.get('classname'),name=test.get('name'),failed=failure is not None,
                          graph_source_lines=sorted(set(lines)),test_source_lines=source_lines,
                          failure_markers=failure_markers,seconds=float(test.get('time','0'))))
sysctl={}
for key in ['apparmor_restrict_unprivileged_userns','unprivileged_userns_clone']:
    p=Path('/proc/sys/kernel')/key
    sysctl[key]=p.read_text().strip() if p.is_file() else None
probe=subprocess.run(['unshare','-Ur','true'],capture_output=True,text=True)
report=dict(log_files_classified=files,markers=dict(counts),exception_classes=dict(classes),missing_libraries=sorted(missing),tests=tests,other_testcase_count=other_tests,
            public_exception_frames=list(traces.values()),
            sysctl=sysctl,user_namespace_probe_exit=probe.returncode,user_namespace_probe_permission_denied='Operation not permitted' in probe.stderr,
            scope='Synthetic CI IDE contexts; only fixed diagnostic markers, public exception classes and test names. No raw logs, messages or paths.')
out=Path(os.environ.get('MCPP_DIAG_REPORT',str(Path(os.environ['RUNNER_TEMP'])/'clion-public-diagnostics.json')))
out.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
