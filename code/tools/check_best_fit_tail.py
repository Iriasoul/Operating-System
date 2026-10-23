#!/usr/bin/env python3
"""Run page-allocator tail regressions in a disposable kernel copy (WSL/Linux).

From code/: python3 tools/check_best_fit_tail.py
Use --case init or --case free to run either regression independently.
Use --manager first_fit to select First-Fit in the temporary kernel only.
The source tree and the original framework checks are not modified.
"""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--qemu', default=os.environ.get('QEMU', 'qemu-system-riscv64'))
    parser.add_argument('--case', choices=('all', 'init', 'free'), default='all')
    parser.add_argument('--manager', choices=('best_fit', 'first_fit'), default='best_fit')
    parser.add_argument('--timeout', type=float, default=15)
    args = parser.parse_args()
    qemu = shutil.which(args.qemu)
    if qemu is None:
        parser.error('QEMU executable not found: ' + args.qemu)

    code = Path(__file__).resolve().parents[1]
    logs = code / ('obj/' + args.manager + '_tail_validation')
    logs.mkdir(parents=True, exist_ok=True)
    version = subprocess.check_output([qemu, '--version'], text=True).splitlines()[0]
    print(version, flush=True)

    with tempfile.TemporaryDirectory(prefix='lab2-' + args.manager + '-tail-') as temporary:
        tree = Path(temporary) / 'code'
        shutil.copytree(code, tree, ignore=shutil.ignore_patterns(
            'obj', 'bin', '__pycache__', '.git'))
        shutil.copyfile(tree / ('tools/test_' + args.manager + '_tail.c'),
                        tree / ('kern/mm/' + args.manager + '_tail_test.c'))
        pmm = tree / 'kern/mm/pmm.c'
        source = pmm.read_text(encoding='utf-8')
        anchor = '    init_pmm_manager();'
        if source.count(anchor) != 1:
            raise RuntimeError('Expected one init_pmm_manager call')
        if args.manager == 'first_fit':
            selection = '    pmm_manager = &best_fit_pmm_manager;'
            if source.count(selection) != 1:
                raise RuntimeError('Expected one default manager selection')
            source = source.replace(selection, '    pmm_manager = &default_pmm_manager;', 1)
        entry = args.manager + '_tail_run_tests'
        call = '\n    %s(%d, %d);' % (entry,
            args.case != 'free', args.case != 'init')
        source = ('void %s(int, int);\n' % entry +
                  source.replace(anchor, anchor + call, 1))
        pmm.write_text(source, encoding='utf-8')

        with (logs / ('build-' + args.case + '.log')).open('w') as output:
            subprocess.run(['make', 'PMM=best_fit', 'QEMU=' + qemu], cwd=tree,
                           stdout=output, stderr=subprocess.STDOUT, check=True)

        run_log = logs / ('run-' + args.case + '.log')
        test_name = 'First-Fit' if args.manager == 'first_fit' else 'Best-Fit'
        label = '[' + test_name + ' Tail Test]'
        marker = label + ' ALL TESTS PASSED'
        output_text = ''
        with run_log.open('w') as output:
            process = subprocess.Popen([
                qemu, '-machine', 'virt', '-nographic', '-bios', 'default',
                '-kernel', str(tree / 'bin/ucore.img')],
                stdout=output, stderr=subprocess.STDOUT)
            try:
                deadline = time.monotonic() + args.timeout
                while time.monotonic() < deadline:
                    output_text = run_log.read_text(errors='replace')
                    if 'panic' in output_text.lower() or process.poll() is not None:
                        break
                    # Also wait for the original framework checks after restoring
                    # the real globals and completing normal page initialization.
                    if marker in output_text and 'check_alloc_page() succeeded!' in output_text:
                        break
                    time.sleep(0.05)
            finally:
                if process.poll() is None:
                    process.terminate()
                    try:
                        process.wait(timeout=3)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait()
        output_text = run_log.read_text(errors='replace')
        for line in output_text.splitlines():
            if any(word in line for word in (label, 'panic',
                                             'assertion failed', 'check_alloc_page()')):
                print(line)
        print('Log:', run_log)
        passed = (marker in output_text and 'check_alloc_page() succeeded!' in output_text
                  and 'panic' not in output_text.lower())
        return 0 if passed else 1


if __name__ == '__main__':
    raise SystemExit(main())
