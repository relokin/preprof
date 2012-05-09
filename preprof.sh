#!/bin/bash

if ! TEMP=`getopt -o +E:o:O:I:C: --long event:,out:,one-thread,offcore_rsp0,ievent,icount -n $0 -- "$@"` ; then
    exit 1
fi

eval set -- "$TEMP"

event_cnt=0
debug_info=0

while : ; do
    case $1 in
        -E|--event)
            export PREPROF_EVENT_${event_cnt}="$2"
	    let event_cnt=${event_cnt}+1
            shift 2
            ;;

	-o|--out)
	    export PREPROF_FILE="$2"
	    shift 2
	    ;;

	-O|--offcore_rsp0)
	    export PREPROF_OFFCORE_RSP0="$2"
	    shift 2
	    ;;

	-I|--ievent)
	    export PREPROF_IEVENT="$2"
	    shift 2
	    ;;

	-C|--icount)
	    export PREPROF_ICOUNT="$2"
	    shift 2
	    ;;

        -h|--help)
            cat <<EOF
@PACKAGE_STRING@

Usage: preprof [OPTIONS...] APPLICATION [ARGUMENTS...]

COMMANDS:
  -h, --help                      Show this help

OPTIONS:
  -E, --event                     Count performance counter event
  -o, --out                       Output file name
  -O, --offcore_rsp0              Count offcore_rsp0 event
  -I, --ievent                    Log snapshot of all counters on IEVENT
  -C, --icount                    Log snapshot after NUM occurances of IEVENT
EOF
            exit 0
            ;;
        --)
            shift
            break
            ;;

        *)
            echo "Parsing failed!" >&2
            exit 1
            ;;
    esac
done

shift $(($OPTIND-1))

if [ x"$1" = x ] ; then
        echo "Please specify an application to profile!" >&2
        exit 1
fi

if [ x"$LD_PRELOAD" = x ] ; then
        export LD_PRELOAD="libpreprof.so"
else
        export LD_PRELOAD="$LD_PRELOAD:libpreprof.so"
fi

export PREPROF_CMD="$@"

exec "$@"
