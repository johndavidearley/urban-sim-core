"""Run M35 benchmark fixtures sequentially and compare warm/cold final state."""
import argparse
import json
import platform
import subprocess
from pathlib import Path


def run(args):
    root=Path(args.artifacts).resolve();root.mkdir(parents=True,exist_ok=True)
    summaries=[]
    for name,source in (("startup32",args.startup_save),("legacy96",args.legacy_save),("mixed96",args.mature_save)):
        profiles={}
        for mode in ("warm","cold"):
            output=root/(name+"-"+mode+".json")
            command=[args.benchmark,str(Path(source).resolve()),str(args.ticks),str(output),mode]
            proc=subprocess.run(command,text=True,capture_output=True)
            if proc.returncode: raise RuntimeError(proc.stderr or proc.stdout)
            data=json.loads(output.read_text());profiles[mode]=data
            summary={"fixture":name,"mode":mode,"input":str(Path(source).resolve()),
                "profile":str(output),"compiler":data["compiler"],"build_config":data["build_config"],
                "tick":data["tick"],"access_plan":data["access_plan"],
                "initial_sample":data["samples"][0],"final_sample":data["samples"][-1]}
            summaries.append(summary)
            print(name,mode,summary["tick"],flush=True)
        for key in ("final_world_checksum","final_funds","final_tourism","final_cargo"):
            assert profiles["warm"][key]==profiles["cold"][key],name+" warm/cold differs: "+key
    result={"machine":platform.machine(),"platform":platform.platform(),"ticks_per_profile":args.ticks,
        "warm_cold_final_state_matches":True,"profiles":summaries}
    (root/"results.json").write_text(json.dumps(result,indent=2))
    print("PASS",root/"results.json",flush=True)


if __name__=="__main__":
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--benchmark",required=True)
    parser.add_argument("--startup-save",required=True)
    parser.add_argument("--legacy-save",required=True)
    parser.add_argument("--mature-save",required=True)
    parser.add_argument("--artifacts",required=True)
    parser.add_argument("--ticks",type=int,default=1000)
    run(parser.parse_args())
