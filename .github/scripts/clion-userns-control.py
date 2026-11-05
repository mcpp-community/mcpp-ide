"""One-host JCEF policy control; only allowlisted results leave the runner."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import time

parser=argparse.ArgumentParser()
parser.add_argument('--plugin',required=True,type=Path)
parser.add_argument('--language',required=True,type=Path)
parser.add_argument('--sha256',required=True)
parser.add_argument('--ide-version',required=True)
args=parser.parse_args()
root=Path.cwd()
output=Path(os.environ['RUNNER_TEMP'])/'clion-userns-control'
output.mkdir(exist_ok=True)
public=Path(os.environ['RUNNER_TEMP'])/'clion-userns-public.json'
policy=Path('/proc/sys/kernel/apparmor_restrict_unprivileged_userns')
graph_source=root/'mide-plugins/clion/integration-tests/src/test/kotlin/GraphInteractionTest.kt'
report={'ide_version':args.ide_version,'plugin_sha256':args.sha256,'phases':[],
        'scope':'Same canonical ZIP, stable IDE and test source. JCEF flags and all test assertions unchanged. No raw logs, messages or paths.'}
started=time.monotonic()

def save():
    public.write_text(json.dumps(report,indent=2)+'\n')

def digest(path):
    value=hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda:stream.read(1024*1024),b''):
            value.update(block)
    return value.hexdigest()

def probe():
    result=subprocess.run(['unshare','-Ur','true'],capture_output=True,text=True)
    return {'exit':result.returncode,'permission_denied':'Operation not permitted' in result.stderr}

def set_policy(value):
    subprocess.run(['sudo','sysctl','-w','kernel.apparmor_restrict_unprivileged_userns='+value],
                   check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
    if policy.read_text().strip()!=value:
        raise RuntimeError('policy readback failed')

def phase(name,patterns):
    if digest(args.plugin)!=args.sha256:
        raise RuntimeError('canonical ZIP identity changed')
    test_root=root/'out/ide-tests/tests'
    before={p.resolve() for p in test_root.iterdir()} if test_root.is_dir() else set()
    directory=output/name
    directory.mkdir(exist_ok=True)
    source_hash=digest(graph_source)
    entry={'phase':name,'policy':policy.read_text().strip(),'userns':probe(),
           'plugin_sha256':digest(args.plugin),'graph_test_source_sha256':source_hash,
           'language_zip_sha256':digest(args.language),
           'test_patterns':patterns}
    command=['xvfb-run','-a','gradle','--no-daemon','--max-workers=4',
             '-p','mide-plugins/clion/integration-tests','test','--rerun-tasks',
             '-PpluginZip='+str(args.plugin.resolve()),'-PmcpplsZip='+str(args.language.resolve()),
             '-PideVersion='+args.ide_version,'-Poutput='+str(directory.resolve())]
    for pattern in patterns:
        command+=['--tests',pattern]
    begin=time.monotonic()
    with (directory/'gradle-private.log').open('w') as log:
        result=subprocess.run(command,stdout=log,stderr=subprocess.STDOUT,timeout=600)
    entry['exit']=result.returncode
    entry['seconds']=time.monotonic()-begin
    if digest(graph_source)!=source_hash or digest(args.plugin)!=args.sha256:
        raise RuntimeError('controlled input changed')
    current=[str(p.resolve()) for p in test_root.iterdir() if p.resolve() not in before] if test_root.is_dir() else []
    contexts=directory/'contexts-private.json'
    contexts.write_text(json.dumps(current))
    env=os.environ.copy()
    env.update(MCPP_DIAG_CONTEXTS=str(contexts),MCPP_DIAG_REPORT=str(directory/'diagnostics.json'))
    with (directory/'classifier-private.log').open('w') as log:
        subprocess.run([sys.executable,'.github/scripts/clion-public-diagnostics.py'],env=env,
                       stdout=log,stderr=subprocess.STDOUT,check=True)
    entry['diagnostics']=json.loads((directory/'diagnostics.json').read_text())
    entry['new_contexts']=len(current)
    entry['graph_checks']={}
    for kind in ['graphs','graphs-no-jcef']:
        checks=directory/kind/'checks.json'
        if checks.is_file():
            values=json.loads(checks.read_text()).get('passed',[])
            entry['graph_checks'][kind]=[s for s in values if isinstance(s,str) and re.fullmatch(r'[A-Za-z0-9 _-]{1,120}',s)]
    report['phases'].append(entry)
    save()
    return entry

original=None
status=1
try:
    if not re.fullmatch(r'[0-9a-f]{64}',args.sha256) or digest(args.plugin)!=args.sha256:
        raise RuntimeError('canonical ZIP identity mismatch')
    report['language_zip_sha256']=digest(args.language)
    original=policy.read_text().strip()
    report['original_policy']=original
    baseline=phase('baseline',['GraphInteractionTest.graphRendersNavigatesAndRecoversInJcef'])
    set_policy('0')
    enabled=probe()
    report['enabled_userns_probe']=enabled
    if enabled['exit']!=0:
        raise RuntimeError('user namespace permission did not change')
    controlled=phase('allow-userns',['GraphInteractionTest'])
    reproduced=any(t['failed'] and t['name']=='graphRendersNavigatesAndRecoversInJcef()'
                   and 184 in t['graph_source_lines'] for t in baseline['diagnostics']['tests'])
    report['baseline_browser_creation_failure_reproduced']=reproduced
    report['same_host_policy_control_passed']=controlled['exit']==0
    transitioned=(original=='1' and baseline['userns']['exit']!=0
                  and baseline['userns']['permission_denied'] and enabled['exit']==0)
    report['host_permission_transition_verified']=transitioned
    # The optional whole suite uses the same ZIP and enabled host policy.
    free=shutil.disk_usage(root).free
    if controlled['exit']==0 and free>=5*1024**3 and time.monotonic()-started<10*60:
        suite=phase('all-scenarios',[])
        report['optional_all_scenarios']='passed' if suite['exit']==0 else 'failed'
        status=int(suite['exit']!=0)
    else:
        report['optional_all_scenarios']='not run: graph failure or remaining resource/time budget'
        status=int(controlled['exit']!=0)
    report['causality']='supported' if reproduced and transitioned and controlled['exit']==0 else 'not established'
except BaseException as error:
    status=1
    report['diagnostic_error_class']=type(error).__name__
finally:
    if original is not None:
        try:
            set_policy(original)
            report['restored_policy']=policy.read_text().strip()
            report['restored_userns_probe']=probe()
        except BaseException as error:
            report['restore_error_class']=type(error).__name__
            status=1
    save()
print(json.dumps(report,indent=2))
raise SystemExit(status)
