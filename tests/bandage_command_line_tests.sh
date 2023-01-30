#!/bin/bash

bandagepath=$1

if [[ ! -f "$bandagepath" ]]; then
    echo "File does not exists: '$bandagepath'"
    echo "Usage: bandage_command_line_tests.sh \$PATH_TO_BANDAGE_EXECUTABLE"
    exit 1
fi

passes=0
fails=0

# This function tests the exit code, stdout and stderr of a command.
function test_all {
    command=$1
    expected_exit_code=$2
    expected_std_out=$3
    expected_std_err=$4

    $command 1> tmp/std_out 2> tmp/std_err
    exit_code=$?
    std_out="$(echo $(cat tmp/std_out))"
    std_err="$(echo $(cat tmp/std_err))"

    if [ $exit_code == $expected_exit_code ]; then correct_exit_code=true; else correct_exit_code=false; fi
    if [ "$std_out" == "$expected_std_out" ]; then correct_std_out=true; else correct_std_out=false; fi
    if [ "$std_err" == "$expected_std_err" ]; then correct_std_err=true; else correct_std_err=false; fi

    if $correct_exit_code && $correct_std_out && $correct_std_err; then
        passes=$((passes + 1))
        echo "PASS: $command";
    else
        fails=$((fails + 1))
        echo "FAIL: $command"
        if ! $correct_exit_code; then echo "   expected exit code: $expected_exit_code"; echo "   actual exit code: $exit_code"; fi
        if ! $correct_std_out; then echo "   expected std out: $expected_std_out"; echo "   actual std out: $std_out"; fi
        if ! $correct_std_err; then echo "   expected std err: $expected_std_err"; echo "   actual std err: $std_err"; fi
    fi

    rm tmp/std_out
    rm tmp/std_err
}

# This function only tests the exit code of a command.
function test_exit_code {
    command=$1
    expected_exit_code=$2

    $command 1> tmp/std_out 2> tmp/std_err
    exit_code=$?

    if [ $exit_code == $expected_exit_code ]; then correct_exit_code=true; else correct_exit_code=false; fi

    if $correct_exit_code && $correct_std_out && $correct_std_err; then
        passes=$((passes + 1))
        echo "PASS: $command";
    else
        fails=$((fails + 1))
        echo "FAIL: $command"
        echo "FAIL: $command"
        if ! $correct_exit_code; then echo "   expected exit code: $expected_exit_code"; echo "   actual exit code: $exit_code"; fi
    fi

    rm tmp/std_out
    rm tmp/std_err
}

# This function tests only the width of an image.
function test_image_height {
    image=$1
    height=$2

    size=`convert $image -print "Size: %wx%h\n" /dev/null`

    if [[ $size == *"x$height"* ]]; then
        passes=$((passes + 1))
        echo "PASS: $size";
    else
        fails=$((fails + 1))
        echo "FAIL: $command"
        echo "FAIL:"
        echo "   expected height: $height"
        echo "   actual: $size"
    fi
}

# This function tests only the width of an image.
function test_image_width {
    image=$1
    width=$2

    size=`convert $image -print "Size: %wx%h\n" /dev/null`

    if [[ $size == *"$width""x"* ]]; then
        passes=$((passes + 1))
        echo "PASS: $size";
    else
        fails=$((fails + 1))
        echo "FAIL: $command"
        echo "FAIL:"
        echo "   expected width: $width"
        echo "   actual: $size"
    fi
}

# This function tests the height and width of an image.
function test_image_width_and_height {
    image=$1
    width=$2
    height=$3
    
    size=`convert $image -print "Size: %wx%h\n" /dev/null`
    expected_size="Size: $width""x""$height"

    if [ "$size" == "$expected_size" ]; then
        passes=$((passes + 1))
        echo "PASS: $size";
    else
        fails=$((fails + 1))
        echo "FAIL: $command"
        echo "FAIL:"
        echo "   expected: $expected_size"
        echo "   actual: $size"
    fi
}


# The tmp directory is used to store files that hold stdout and stderr as well as anything else made by the commands.
mkdir tmp

# Bandage image tests
test_all "$bandagepath image inputs/test.fastg tmp/test.png" 0 "" ""
test_image_height tmp/test.png 1000; rm tmp/test.png
test_all "$bandagepath image inputs/test.fastg tmp/test.jpg" 0 "" "";
test_image_height tmp/test.jpg 1000; rm tmp/test.jpg
test_all "$bandagepath image inputs/test.fastg tmp/test.svg" 0 "" ""; rm tmp/test.svg
test_all "$bandagepath image inputs/test.fastg tmp/test.png --height 500" 0 "" ""
test_image_height tmp/test.png 500; rm tmp/test.png
test_all "$bandagepath image inputs/test.fastg tmp/test.png --height 50" 0 "" ""
test_image_height tmp/test.png 50; rm tmp/test.png
test_all "$bandagepath image inputs/test.fastg tmp/test.png --width 500" 0 "" ""
test_image_width tmp/test.png 500; rm tmp/test.png
test_all "$bandagepath image inputs/test.fastg tmp/test.png --width 50" 0 "" ""
test_image_width tmp/test.png 50; rm tmp/test.png
test_all "$bandagepath image inputs/test.fastg tmp/test.png  --width 400 --height 500" 0 "" ""
test_image_width_and_height tmp/test.png 400 500; rm tmp/test.png
test_all "$bandagepath image inputs/test.fastg tmp/test.png  --width 500 --height 400" 0 "" ""
test_image_width_and_height tmp/test.png 500 400; rm tmp/test.png
test_all "$bandagepath image abc.fastg test.png" 105 "" "<graph>: File does not exist: abc.fastg Run with --help or --helpall for more information."
test_all "$bandagepath image inputs/test.fastg test.abc" 1 "" "Bandage-NG error: the output filename must end in .png, .jpg or .svg"
test_all "$bandagepath image inputs/test.csv tmp/test.png" 1 "" "Bandage-NG error: could not load inputs/test.csv"
test_all "$bandagepath image inputs/test.fastg test.png --query abc.fasta" 105 "" "--query: File does not exist: abc.fasta Run with --help or --helpall for more information."
test_all "$bandagepath image inputs/test_rgfa.gfa test.png --colour gfa" 0 "" ""
test_all "$bandagepath image inputs/test.gfa test.png --colour gc" 0 "" ""

# BandageNG info tests
test_all "$bandagepath info inputs/test.gfa --tsv" 0 "inputs/test.gfa 17 16 60 60 30959 29939 10 29.4118% 1 30959 0 2060 119 2001 2060 2060 2060 532.042 25939" ""

# BandageNG load tests
test_all "$bandagepath load abc.fastg" 105 "" "<graph>: File does not exist: abc.fastg Run with --help or --helpall for more information."
test_all "$bandagepath load inputs/test.fastg --query abc.fasta" 105 "" "--query: File does not exist: abc.fasta Run with --help or --helpall for more information."

# BandageNG help tests
test_exit_code "$bandagepath --help" 0
test_exit_code "$bandagepath --helpall" 0
test_exit_code "$bandagepath --version" 0

# BandageNG incorrect settings tests
test_all "$bandagepath --abc" 109 "" "The following argument was not expected: --abc Run with --help or --helpall for more information."
test_all "$bandagepath --scope" 114 "" "--scope: 1 required SCOPE:value in {entire->0,aroundnodes->1,aroundblast->3,depthrange->4} OR {0,1,3,4} missing Run with --help or --helpall for more information."
test_all "$bandagepath --scope abc" 105 "" "--scope: Check abc value in {entire->0,aroundnodes->1,aroundblast->3,depthrange->4} OR {0,1,3,4} FAILED Run with --help or --helpall for more information."
test_all "$bandagepath --nodes" 114 "" "--nodes: 1 required TEXT missing Run with --help or --helpall for more information."
test_all "$bandagepath --distance" 114 "" "--distance: 1 required INT:INT in [0 - 100] missing Run with --help or --helpall for more information."
test_all "$bandagepath --distance abc" 105 "" "--distance: Value abc not in range [0 - 100] Run with --help or --helpall for more information."
test_all "$bandagepath --mindepth" 114 "" "--mindepth: 1 required FLOAT:FLOAT in [0 - 1e+06] missing Run with --help or --helpall for more information."
test_all "$bandagepath --mindepth abc" 105 "" "--mindepth: Value abc not in range [0 - 1e+06] Run with --help or --helpall for more information."
test_all "$bandagepath --maxdepth" 114 "" "--maxdepth: 1 required FLOAT:FLOAT in [0 - 1e+06] missing Run with --help or --helpall for more information."
test_all "$bandagepath --nodelen"  114 "" "--nodelen: 1 required FLOAT:FLOAT in [0 - 1e+06] missing Run with --help or --helpall for more information."
test_all "$bandagepath --minnodlen" 114 "" "--minnodlen: 1 required FLOAT:FLOAT in [1 - 100] missing Run with --help or --helpall for more information."
test_all "$bandagepath --edgelen" 114 "" "--edgelen: 1 required FLOAT:FLOAT in [0.1 - 100] missing Run with --help or --helpall for more information."
test_all "$bandagepath --edgewidth" 114 "" "--edgewidth: 1 required FLOAT:FLOAT in [0.1 - 100] missing Run with --help or --helpall for more information."
test_all "$bandagepath --doubsep" 114 "" "--doubsep: 1 required FLOAT:FLOAT in [0 - 100] missing Run with --help or --helpall for more information."
test_all "$bandagepath --nodseglen" 114 "" "--nodseglen: 1 required FLOAT:FLOAT in [1 - 1000] missing Run with --help or --helpall for more information."
test_all "$bandagepath --iter" 114 "" "--iter: 1 required INT:INT in [0 - 4] missing Run with --help or --helpall for more information."
test_all "$bandagepath --nodewidth" 114 "" "--nodewidth: 1 required FLOAT:FLOAT in [0.5 - 1000] missing Run with --help or --helpall for more information."
test_all "$bandagepath --depwidth" 114 "" "--depwidth: 1 required FLOAT:FLOAT in [0 - 1] missing Run with --help or --helpall for more information."
test_all "$bandagepath --deppower" 114 "" "--deppower: 1 required FLOAT:FLOAT in [0 - 1] missing Run with --help or --helpall for more information."
test_all "$bandagepath --fontsize" 114 "" "--fontsize: 1 required INT:INT in [1 - 100] missing Run with --help or --helpall for more information."
test_all "$bandagepath --edgecol" 114 "" "--edgecol: 1 required COLOR missing Run with --help or --helpall for more information."
test_all "$bandagepath --edgecol abc" 105 "" "--edgecol: This is not a valid color name: abc Run with --help or --helpall for more information."
test_all "$bandagepath --outcol" 114 "" "--outcol: 1 required COLOR missing Run with --help or --helpall for more information."
test_all "$bandagepath --outline" 114 "" "--outline: 1 required FLOAT:FLOAT in [0 - 100] missing Run with --help or --helpall for more information."
test_all "$bandagepath --selcol" 114 "" "--selcol: 1 required COLOR missing Run with --help or --helpall for more information."
test_all "$bandagepath --textcol" 114 "" "--textcol: 1 required COLOR missing Run with --help or --helpall for more information."
test_all "$bandagepath --toutcol" 114 "" "--toutcol: 1 required COLOR missing Run with --help or --helpall for more information."
test_all "$bandagepath --toutline" 114 "" "--toutline: 1 required FLOAT:FLOAT in [0 - 10] missing Run with --help or --helpall for more information."
test_all "$bandagepath --colour" 114 "" "--colour: 1 required :value in {random->1,uniform->2,depth->3,custom->5,gc->6,gfa->7,csv->8} OR {1,2,3,5,6,7,8} missing Run with --help or --helpall for more information."
test_all "$bandagepath --colour abc" 105 "" "--colour: Check abc value in {random->1,uniform->2,depth->3,custom->5,gc->6,gfa->7,csv->8} OR {1,2,3,5,6,7,8} FAILED Run with --help or --helpall for more information."
test_all "$bandagepath --ransatpos" 114 "" "--ransatpos: 1 required INT:INT in [0 - 255] missing Run with --help or --helpall for more information."
test_all "$bandagepath --ransatneg" 114 "" "--ransatneg: 1 required INT:INT in [0 - 255] missing Run with --help or --helpall for more information."
test_all "$bandagepath --ranligpos" 114 "" "--ranligpos: 1 required INT:INT in [0 - 255] missing Run with --help or --helpall for more information."
test_all "$bandagepath --ranligneg" 114 "" "--ranligneg: 1 required INT:INT in [0 - 255] missing Run with --help or --helpall for more information."
test_all "$bandagepath --ranopapos" 114 "" "--ranopapos: 1 required INT:INT in [0 - 255] missing Run with --help or --helpall for more information."
test_all "$bandagepath --ranopaneg" 114 "" "--ranopaneg: 1 required INT:INT in [0 - 255] missing Run with --help or --helpall for more information."
test_all "$bandagepath --unicolpos" 114 "" "--unicolpos: 1 required COLOR missing Run with --help or --helpall for more information."
test_all "$bandagepath --unicolneg" 114 "" "--unicolneg: 1 required COLOR missing Run with --help or --helpall for more information."
test_all "$bandagepath --unicolspe" 114 "" "--unicolspe: 1 required COLOR missing Run with --help or --helpall for more information."
#test_all "$bandagepath --depcollow" 114 "" "Bandage-NG error: --depcollow must be followed by a 6-digit hex colour (e.g. #FFB6C1), an 8-digit hex colour (e.g. #7FD2B48C) or a standard colour name (e.g. skyblue)"
#test_all "$bandagepath --depcolhi"  114 "" "Bandage-NG error: --depcolhi must be followed by a 6-digit hex colour (e.g. #FFB6C1), an 8-digit hex colour (e.g. #7FD2B48C) or a standard colour name (e.g. skyblue)"
test_all "$bandagepath --depvallow" 114 "" "--depvallow: 1 required FLOAT:FLOAT in [0 - 1e+06] missing Run with --help or --helpall for more information."
test_all "$bandagepath --depvalhi" 114 "" "--depvalhi: 1 required FLOAT:FLOAT in [0 - 1e+06] missing Run with --help or --helpall for more information."
test_all "$bandagepath --query"    114 "" "--query: 1 required TEXT:FILE missing Run with --help or --helpall for more information."
test_all "$bandagepath --blastp"   114 "" "--blastp: 1 required TEXT missing Run with --help or --helpall for more information."
test_all "$bandagepath --alfilter" 114 "" "--alfilter: 1 required INT:INT in [1 - 1000000] missing Run with --help or --helpall for more information."
test_all "$bandagepath --qcfilter" 114 "" "--qcfilter: 1 required FLOAT:FLOAT in [0 - 100] missing Run with --help or --helpall for more information."
test_all "$bandagepath --ifilter"  114 "" "--ifilter: 1 required FLOAT:FLOAT in [0 - 100] missing Run with --help or --helpall for more information."
test_all "$bandagepath --evfilter" 114 "" "--evfilter: 1 required SCINOT:FLOAT in [1e-999 - 99] missing Run with --help or --helpall for more information."
test_all "$bandagepath --evfilter abc" 105 "" "--evfilter: Value abc not in range [1e-999 - 99] Run with --help or --helpall for more information."
# test_all "$bandagepath --evfilter 1" 0 "" ""
# test_all "$bandagepath --evfilter 1.0" 0 "" ""
test_all "$bandagepath --evfilter e1" 105 "" "--evfilter: Value e1 not in range [1e-999 - 99] Run with --help or --helpall for more information."
test_all "$bandagepath --bsfilter"  114 "" "--bsfilter: 1 required FLOAT:FLOAT in [0 - 1e+06] missing Run with --help or --helpall for more information."
test_all "$bandagepath --pathnodes" 114 "" "--pathnodes: 1 required INT:INT in [1 - 50] missing Run with --help or --helpall for more information."
test_all "$bandagepath --minpatcov" 114 "" "--minpatcov: 1 required FLOAT:FLOAT in [0.3 - 1] missing Run with --help or --helpall for more information."
test_all "$bandagepath --minhitcov" 114 "" "--minhitcov: 1 required FLOAT:FLOAT in [0.3 - 1], or \"off\" missing Run with --help or --helpall for more information."
test_all "$bandagepath --minmeanid" 114 "" "--minmeanid: 1 required FLOAT:FLOAT in [0 - 1], or \"off\" missing Run with --help or --helpall for more information."
test_all "$bandagepath --minpatlen" 114 "" "--minpatlen: 1 required FLOAT:FLOAT in [0 - 10000], or \"off\" missing Run with --help or --helpall for more information."
test_all "$bandagepath --maxpatlen" 114 "" "--maxpatlen: 1 required FLOAT:FLOAT in [0 - 10000], or \"off\" missing Run with --help or --helpall for more information."
test_all "$bandagepath --minlendis" 114 "" "--minlendis: 1 required INT:INT in [-1000000 - 1000000], or \"off\" missing Run with --help or --helpall for more information."
test_all "$bandagepath --maxlendis" 114 "" "--maxlendis: 1 required INT:INT in [-1000000 - 1000000], or \"off\" missing Run with --help or --helpall for more information."
test_all "$bandagepath --maxevprod" 114 "" "--maxevprod: 1 required SCINOT:FLOAT in [1e-999 - 99], or \"off\" missing Run with --help or --helpall for more information."


rmdir tmp

echo ""
echo "$passes tests passed, $fails tests failed"
if [[ $fails != 0 ]]; then
    exit 1;
fi
