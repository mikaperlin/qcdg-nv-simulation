#!/usr/bin/env python
import sys, os, subprocess
from basename import basename

whide_flag = "whide"

if len(sys.argv) < 8:
    print("usage: " + sys.argv[0] + " [sim_args...] walltime_in_hours" + \
          " [{}]".format(whide_flag))
    exit(1)

whide = whide_flag in sys.argv
if whide: sys.argv.remove(whide_flag)

walltime_in_hours = sys.argv[-1]
sim_args = sys.argv[1:-1]

nodes = 1
tasks_per_node = 16
task_num = str(int(nodes)*tasks_per_node)

log10_samples = sim_args[sim_args.index("--log10_samples")+1]
mem_per_task_estimate_in_mb = int(0.5 * 4**float(log10_samples))

project_dir = os.path.dirname(os.path.realpath(__file__))
job_dir = "jobs"
mkfac = "mkfac.py"
script = "fidelity-sweep.py"
job_file = job_dir+"/"+basename(sim_args)+".sh"
out_file = job_dir+"/"+basename(sim_args)+".o"

resources = ["nodes={}:ppn={}".format(nodes,tasks_per_node),
             "walltime={}:00:00".format(walltime_in_hours),
             "pmem={}mb".format(mem_per_task_estimate_in_mb)]
options = ["-N "+basename(sim_args), "-m e", "-j oe", "-o "+out_file]
for resource in resources:
    options += ["-l "+resource]

job_text = "#!/usr/bin/env sh\n"
for option in options:
    job_text += "#MSUB {}\n".format(option)
job_text += "\n"
job_text += "python {}/{} {} {}\n".format(project_dir,script," ".join(sim_args),task_num)


with open(job_file,"w") as f: f.write(job_text)

subprocess.call(["./"+mkfac] + ([ whide_flag ] if whide else []))
subprocess.call(["msub",job_file])
