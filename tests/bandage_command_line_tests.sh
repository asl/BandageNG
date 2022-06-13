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
test_all "$bandagepath image abc.fastg test.png" 1 "" "Bandage-NG error: abc.fastg does not exist"
test_all "$bandagepath image inputs/test.fastg test.abc" 1 "" "Bandage-NG error: the output filename must end in .png, .jpg or .svg"
test_all "$bandagepath image inputs/test.csv tmp/test.png" 1 "" "Bandage-NG error: could not load inputs/test.csv"
test_all "$bandagepath image inputs/test.fastg test.png --query abc.fasta" 1 "" "Bandage-NG error: --query must be followed by a valid filename"
test_all "$bandagepath image inputs/test_rgfa.gfa test.png --colour gfa" 0 "" ""

# BandageNG load  tests
test_all "$bandagepath load abc.fastg" 1 "" "Bandage-NG error: abc.fastg does not exist"
test_all "$bandagepath load inputs/test.fastg --query abc.fasta" 1 "" "Bandage-NG error: --query must be followed by a valid filename"

# Bandage help tests
test_exit_code "$bandagepath --help" 0
test_exit_code "$bandagepath --helpall" 0
test_exit_code "$bandagepath --version" 0

# Bandage incorrect settings tests
test_all "$bandagepath --abc" 1 "" "Bandage-NG error: Invalid option: --abc"
test_all "$bandagepath --scope" 1 "" "Bandage-NG error: --scope must be followed by entire, aroundnodes, aroundblast or depthrange"
test_all "$bandagepath --scope abc" 1 "" "Bandage-NG error: --scope must be followed by entire, aroundnodes, aroundblast or depthrange"
test_all "$bandagepath --nodes" 1 "" "Bandage-NG error: --nodes must be followed by a list of node names"
test_all "$bandagepath --distance" 1 "" "Bandage-NG error: --distance must be followed by an integer"
test_all "$bandagepath --distance abc" 1 "" "Bandage-NG error: --distance must be followed by an integer"
test_all "$bandagepath --mindepth" 1 "" "Bandage-NG error: --mindepth must be followed by a number"
test_all "$bandagepath --mindepth abc" 1 "" "Bandage-NG error: --mindepth must be followed by a number"
test_all "$bandagepath --maxdepth" 1 "" "Bandage-NG error: --maxdepth must be followed by a number"
test_all "$bandagepath --nodelen" 1 "" "Bandage-NG error: --nodelen must be followed by a number"
test_all "$bandagepath --minnodlen" 1 "" "Bandage-NG error: --minnodlen must be followed by a number"
test_all "$bandagepath --edgelen" 1 "" "Bandage-NG error: --edgelen must be followed by a number"
test_all "$bandagepath --edgewidth" 1 "" "Bandage-NG error: --edgewidth must be followed by a number"
test_all "$bandagepath --doubsep" 1 "" "Bandage-NG error: --doubsep must be followed by a number"
test_all "$bandagepath --nodseglen" 1 "" "Bandage-NG error: --nodseglen must be followed by a number"
test_all "$bandagepath --iter" 1 "" "Bandage-NG error: --iter must be followed by an integer"
test_all "$bandagepath --nodewidth" 1 "" "Bandage-NG error: --nodewidth must be followed by a number"
test_all "$bandagepath --depwidth" 1 "" "Bandage-NG error: --depwidth must be followed by a number"
test_all "$bandagepath --deppower" 1 "" "Bandage-NG error: --deppower must be followed by a number"
test_all "$bandagepath --fontsize" 1 "" "Bandage-NG error: --fontsize must be followed by an integer"
test_all "$bandagepath --edgecol" 1 "" "Bandage-NG error: --edgecol must be followed by a 6-digit hex colour (e.g. #FFB6C1), an 8-digit hex colour (e.g. #7FD2B48C) or a standard colour name (e.g. skyblue)"
test_all "$bandagepath --edgecol abc" 1 "" "Bandage-NG error: --edgecol must be followed by a 6-digit hex colour (e.g. #FFB6C1), an 8-digit hex colour (e.g. #7FD2B48C) or a standard colour name (e.g. skyblue)"
test_all "$bandagepath --outcol" 1 "" "Bandage-NG error: --outcol must be followed by a 6-digit hex colour (e.g. #FFB6C1), an 8-digit hex colour (e.g. #7FD2B48C) or a standard colour name (e.g. skyblue)"
test_all "$bandagepath --outline" 1 "" "Bandage-NG error: --outline must be followed by a number"
test_all "$bandagepath --selcol" 1 "" "Bandage-NG error: --selcol must be followed by a 6-digit hex colour (e.g. #FFB6C1), an 8-digit hex colour (e.g. #7FD2B48C) or a standard colour name (e.g. skyblue)"
test_all "$bandagepath --textcol" 1 "" "Bandage-NG error: --textcol must be followed by a 6-digit hex colour (e.g. #FFB6C1), an 8-digit hex colour (e.g. #7FD2B48C) or a standard colour name (e.g. skyblue)"
test_all "$bandagepath --toutcol" 1 "" "Bandage-NG error: --toutcol must be followed by a 6-digit hex colour (e.g. #FFB6C1), an 8-digit hex colour (e.g. #7FD2B48C) or a standard colour name (e.g. skyblue)"
test_all "$bandagepath --toutline" 1 "" "Bandage-NG error: --toutline must be followed by a number"
test_all "$bandagepath --colour" 1 "" "Bandage-NG error: --colour must be followed by random, uniform, depth, blastsolid, blastrainbow, custom, gc or gfa"
test_all "$bandagepath --colour abc" 1 "" "Bandage-NG error: --colour must be followed by random, uniform, depth, blastsolid, blastrainbow, custom, gc or gfa"
test_all "$bandagepath --ransatpos" 1 "" "Bandage-NG error: --ransatpos must be followed by an integer"
test_all "$bandagepath --ransatneg" 1 "" "Bandage-NG error: --ransatneg must be followed by an integer"
test_all "$bandagepath --ranligpos" 1 "" "Bandage-NG error: --ranligpos must be followed by an integer"
test_all "$bandagepath --ranligneg" 1 "" "Bandage-NG error: --ranligneg must be followed by an integer"
test_all "$bandagepath --ranopapos" 1 "" "Bandage-NG error: --ranopapos must be followed by an integer"
test_all "$bandagepath --ranopaneg" 1 "" "Bandage-NG error: --ranopaneg must be followed by an integer"
test_all "$bandagepath --unicolpos" 1 "" "Bandage-NG error: --unicolpos must be followed by a 6-digit hex colour (e.g. #FFB6C1), an 8-digit hex colour (e.g. #7FD2B48C) or a standard colour name (e.g. skyblue)"
test_all "$bandagepath --unicolneg" 1 "" "Bandage-NG error: --unicolneg must be followed by a 6-digit hex colour (e.g. #FFB6C1), an 8-digit hex colour (e.g. #7FD2B48C) or a standard colour name (e.g. skyblue)"
test_all "$bandagepath --unicolspe" 1 "" "Bandage-NG error: --unicolspe must be followed by a 6-digit hex colour (e.g. #FFB6C1), an 8-digit hex colour (e.g. #7FD2B48C) or a standard colour name (e.g. skyblue)"
test_all "$bandagepath --depcollow" 1 "" "Bandage-NG error: --depcollow must be followed by a 6-digit hex colour (e.g. #FFB6C1), an 8-digit hex colour (e.g. #7FD2B48C) or a standard colour name (e.g. skyblue)"
test_all "$bandagepath --depcolhi" 1 "" "Bandage-NG error: --depcolhi must be followed by a 6-digit hex colour (e.g. #FFB6C1), an 8-digit hex colour (e.g. #7FD2B48C) or a standard colour name (e.g. skyblue)"
test_all "$bandagepath --depvallow" 1 "" "Bandage-NG error: --depvallow must be followed by a number"
test_all "$bandagepath --depvalhi" 1 "" "Bandage-NG error: --depvalhi must be followed by a number"
test_all "$bandagepath --query" 1 "" "Bandage-NG error: A graph must be given (e.g. via BandageNG load) to use the --query option"
test_all "$bandagepath --blastp" 1 "" "Bandage-NG error: --blastp must be followed by blastn/tblastn parameters"
test_all "$bandagepath --alfilter" 1 "" "Bandage-NG error: --alfilter must be followed by an integer"
test_all "$bandagepath --qcfilter" 1 "" "Bandage-NG error: --qcfilter must be followed by a number"
test_all "$bandagepath --ifilter" 1 "" "Bandage-NG error: --ifilter must be followed by a number"
test_all "$bandagepath --evfilter" 1 "" "Bandage-NG error: --evfilter must be followed by a number in scientific notation"
test_all "$bandagepath --evfilter abc" 1 "" "Bandage-NG error: --evfilter must be followed by a number in scientific notation"
test_all "$bandagepath --evfilter 1" 1 "" "Bandage-NG error: --evfilter must be followed by a number in scientific notation"
test_all "$bandagepath --evfilter 1.0" 1 "" "Bandage-NG error: --evfilter must be followed by a number in scientific notation"
test_all "$bandagepath --evfilter e1" 1 "" "Bandage-NG error: --evfilter must be followed by a number in scientific notation"
test_all "$bandagepath --bsfilter" 1 "" "Bandage-NG error: --bsfilter must be followed by a number"
test_all "$bandagepath --pathnodes" 1 "" "Bandage-NG error: --pathnodes must be followed by an integer"
test_all "$bandagepath --minpatcov" 1 "" "Bandage-NG error: --minpatcov must be followed by a number"
test_all "$bandagepath --minhitcov" 1 "" "Bandage-NG error: --minhitcov must be followed by a number"
test_all "$bandagepath --minmeanid" 1 "" "Bandage-NG error: --minmeanid must be followed by a number"
test_all "$bandagepath --minpatlen" 1 "" "Bandage-NG error: --minpatlen must be followed by a number"
test_all "$bandagepath --maxpatlen" 1 "" "Bandage-NG error: --maxpatlen must be followed by a number"
test_all "$bandagepath --minlendis" 1 "" "Bandage-NG error: --minlendis must be followed by an integer"
test_all "$bandagepath --maxlendis" 1 "" "Bandage-NG error: --maxlendis must be followed by an integer"
test_all "$bandagepath --maxevprod" 1 "" "Bandage-NG error: --maxevprod must be followed by a number in scientific notation"


rmdir tmp

echo ""
echo "$passes tests passed, $fails tests failed"
if [[ $fails != 0 ]]; then
    exit 1;
fi
