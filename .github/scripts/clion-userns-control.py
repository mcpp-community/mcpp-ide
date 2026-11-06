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
parser.add_argument('--focus',choices=['policy-control','packaged-262','native-262'],default='policy-control')
parser.add_argument('--repetitions',choices=[1,3],default=1,type=int)
args=parser.parse_args()
root=Path.cwd()
output=Path(os.environ['RUNNER_TEMP'])/'clion-userns-control'
output.mkdir(exist_ok=True)
public=Path(os.environ['RUNNER_TEMP'])/'clion-userns-public.json'
policy=Path('/proc/sys/kernel/apparmor_restrict_unprivileged_userns')
graph_source=root/'mide-plugins/clion/integration-tests/src/test/kotlin/GraphInteractionTest.kt'
report={'ide_version':args.ide_version,'plugin_sha256':args.sha256,'phases':[],
        'scope':'Same canonical ZIP, stable IDE and test source. JCEF flags and all test assertions unchanged. No raw logs, messages or paths.'}
if args.focus=='native-262':
    report['scope']='Stock CMake/GDB control on stable CLion262 with the usual IDE driver instrumentation. Runtime assertions verify mcpp and mcppls are absent. Canonical ZIP identity is retained but that ZIP is not installed. Only allowlisted markers and public exception frames leave the runner.'
started=time.monotonic()

def context_directories(test_root):
    prefixes=('mcpp-graphs-','mcpp-settings-','mcpp-clion-stable-','native-debug-control-')
    return {p.resolve() for p in test_root.rglob('*') if p.is_dir() and p.name.startswith(prefixes)} if test_root.is_dir() else set()

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
    before=context_directories(test_root)
    directory=output/name
    directory.mkdir(exist_ok=True)
    source_hash=digest(graph_source)
    entry={'phase':name,'policy':policy.read_text().strip(),'userns':probe(),
           'plugin_sha256':digest(args.plugin),'graph_test_source_sha256':source_hash,
           'language_zip_sha256':digest(args.language),
           'test_patterns':patterns}
    if args.focus=='native-262':
        entry['native_test_source_sha256']=digest(root/'mide-plugins/clion/integration-tests/src/test/kotlin/NativeDebuggerControlTest.kt')
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
    current=[str(p) for p in sorted(context_directories(test_root)-before)]
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
    checks=directory/'checks.json'
    allowed_checks={'protocol2-startup','offered-actions-and-forward-targets','visible-three-view-tool-window',
                    'program-executed','compiler-error-visible','native-debugger-process-and-view',
                    'debug-variable-40','debug-variable-42','debug-breakpoint','debug-step-over',
                    'debug-resume-program-output','debug-stop-prevents-program-work','view-Libraries','view-Cache',
                    'mouse-tab-Libraries','mouse-tab-Cache','manifest-completion-and-insertion',
                    'manifest-completion-during-indexing','cache-inventory-visible'}
    allowed_checks.update('action-'+s for s in ['build','run','test','build-failure','build-recovery',
                          'test-failure','test-recovery','debug-native-launch','debug-breakpoint-step-resume','debug-stop'])
    allowed_checks.update('native-test-tree-'+s for s in ['test','test-failure','test-recovery'])
    entry['packaged_checks']=[v for v in json.loads(checks.read_text()).get('passed',[]) if v in allowed_checks] if checks.is_file() else []
    entry['packaged_result_published']=(directory/'result.json').is_file()
    native=directory/'native-debug/checks.json'
    allowed_native={'stock-compiler-control','mcpp-and-mcppls-absent','stock-toolchain-wizard-defaults','stock-cmake-configuration','native-breakpoint',
                    'native-variable-40','native-step','native-variable-42','native-resume-output','native-stop-prevents-work'}
    entry['native_checks']=[v for v in json.loads(native.read_text()).get('passed',[]) if v in allowed_native] if native.is_file() else []
    entry['native_result_published']=(directory/'native-debug/result.json').is_file()
    toolchain=directory/'native-debug/toolchain.json'
    compiler_version=json.loads(toolchain.read_text()).get('compilerVersion') if toolchain.is_file() else None
    entry['native_compiler_version']=compiler_version if isinstance(compiler_version,str) and re.fullmatch(r'[0-9]+(\.[0-9]+){1,3}',compiler_version) else None
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
    if args.focus in ['packaged-262','native-262']:
        if args.ide_version!='2026.2.4.1':
            raise ValueError('focused diagnosis requires stable 262')
        set_policy('0')
        if probe()['exit']!=0:
            raise RuntimeError('user namespace permission did not change')
        native=args.focus=='native-262'
        label='native' if native else 'packaged'
        test='NativeDebuggerControlTest' if native else 'PackagedPluginTest'
        results=[phase(label+'-only' if args.repetitions==1 else label+'-'+str(n),[test])
                 for n in range(1,args.repetitions+1)]
        status=int(any(p['exit']!=0 for p in results))
        report['focused_native_passed' if native else 'focused_packaged_passed']=status==0
        report['fixed_repetitions']=args.repetitions
        report['repetition_passes']=sum(p['exit']==0 for p in results)
        raise SystemExit(status)
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
except SystemExit:
    pass
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
