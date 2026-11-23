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
test_all "$bandagepath image inputs/test.fastg tmp/test.jpg" 0 "" ""
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
test_all "$bandagepath image abc.fastg tmp/test.png" 105 "" "<graph>: File does not exist: abc.fastg Run with --help or --helpall for more information."
test_all "$bandagepath image inputs/test.fastg tmp/test.abc" 1 "" "Bandage-NG error: the output filename must end in .png, .jpg or .svg"
test_all "$bandagepath image inputs/test.csv tmp/test.png" 1 "" "Bandage-NG error: could not load inputs/test.csv: graph format not recognised: inputs/test.csv"
test_all "$bandagepath image inputs/test.fastg tmp/test.png --query abc.fasta" 105 "" "--query: File does not exist: abc.fasta Run with --help or --helpall for more information."
test_all "$bandagepath image inputs/test_rgfa.gfa tmp/test.png --colour gfa" 0 "" ""; rm tmp/test.png
test_all "$bandagepath image inputs/test.gfa tmp/test.png --colour gc" 0 "" ""; rm tmp/test.png

count_layout_nodes() {
    python3 -c 'import json,sys; print(len(json.load(open(sys.argv[1]))))' "$1"
}

count_svg_paths() {
    python3 -c 'import sys; print(open(sys.argv[1]).read().count("<path"))' "$1"
}

test_layout_nodes() {
    graph=$1
    flags=$2
    expected=$3

    test_all "$bandagepath layout $graph tmp/draw.layout $flags" 0 "" ""
    got=$(count_layout_nodes tmp/draw.layout)
    if [[ $got == "$expected" ]]; then
        passes=$((passes + 1))
        echo "PASS: $got layout nodes ($graph $flags)"
    else
        fails=$((fails + 1))
        echo "FAIL: layout nodes ($graph $flags)"
        echo "   expected: $expected"
        echo "   actual: $got"
    fi
    rm -f tmp/draw.layout
}

test_svg_paths() {
    graph=$1
    flags=$2
    expected=$3
    stderr=$4

    test_all "$bandagepath image $graph tmp/draw.svg $flags" 0 "" "$stderr"
    got=$(count_svg_paths tmp/draw.svg)
    if [[ $got == "$expected" ]]; then
        passes=$((passes + 1))
        echo "PASS: $got svg paths ($graph $flags)"
    else
        fails=$((fails + 1))
        echo "FAIL: svg paths ($graph $flags)"
        echo "   expected: $expected"
        echo "   actual: $got"
    fi
    rm -f tmp/draw.svg
}

test_layout_nodes inputs/test.gfa "" 17
test_layout_nodes inputs/test.gfa "--double" 34
test_layout_nodes inputs/test.gfa "--linear" 17
test_layout_nodes inputs/test.gfa "--double --linear" 34
test_layout_nodes inputs/test_draw_corners.gfa "" 3
test_layout_nodes inputs/test_draw_corners.gfa "--double" 5
test_layout_nodes inputs/test_draw_corners.gfa "--linear" 3
test_layout_nodes inputs/test_draw_corners.gfa "--double --linear" 5

test_svg_paths inputs/test.gfa "" 50 ""
test_svg_paths inputs/test.gfa "--double" 100 ""
test_svg_paths inputs/test_draw_corners.gfa "" 11 "QGraphicsScene::addItem: item has already been added to this scene"
test_svg_paths inputs/test_draw_corners.gfa "--double" 18 "QGraphicsScene::addItem: item has already been added to this scene"

test_all "$bandagepath image inputs/test.gfa tmp/test.png --double" 0 "" ""
test_image_height tmp/test.png 1000; rm tmp/test.png
test_all "$bandagepath image inputs/test_draw_corners.gfa tmp/test.png --linear --double" 0 "" "QGraphicsScene::addItem: item has already been added to this scene"
test_image_height tmp/test.png 1000; rm tmp/test.png

# BandageNG info tests
test_all "$bandagepath info inputs/test.gfa --tsv" 0 "inputs/test.gfa 17 16 60 60 30959 29939 10 29.4118% 1 30959 0 2060 119 2001 2060 2060 2060 532.042 25939" ""
test_all "$bandagepath info inputs/test_draw_corners.gfa --tsv" 0 "inputs/test_draw_corners.gfa 4 5 0 0 16 16 1 12.5% 1 12 0 4 4 4 4 4 4 0 0" ""

# BandageNG reduce tests
check_gfa_names() {
    file=$1
    kind=$2
    present=$3
    absent=$4
    nameField=${5:-2}
    ok=true
    for name in $present; do
        if ! awk -F'\t' -v k="$kind" -v n="$name" -v f="$nameField" '$1==k && $f==n {found=1} END{exit !found}' "$file"; then
            echo "FAIL: $file missing $kind $name"
            ok=false
        fi
    done
    for name in $absent; do
        if awk -F'\t' -v k="$kind" -v n="$name" -v f="$nameField" '$1==k && $f==n {found=1} END{exit !found}' "$file"; then
            echo "FAIL: $file unexpected $kind $name"
            ok=false
        fi
    done
    if $ok; then
        passes=$((passes + 1))
        echo "PASS: $file $kind names"
    else
        fails=$((fails + 1))
    fi
}

test_all "$bandagepath reduce inputs/test_components.gfa tmp/component.gfa --scope aroundcomponent --nodes a1" 0 "" ""
test_all "$bandagepath reduce inputs/test_components.gfa tmp/missing-node.gfa --scope aroundcomponent --nodes zzz" 1 "" "The following nodes are not in the graph: zzz"
check_gfa_names tmp/component.gfa S "a1 a2 a3" "b1 b2 c1"
check_gfa_names tmp/component.gfa P "PATH_A" "PATH_B"
check_gfa_names tmp/component.gfa W "WALK_A" "WALK_B" 4
if awk -F'\t' '$1=="P" && $4!="*" {bad=1} END{exit bad}' tmp/component.gfa; then
    passes=$((passes + 1))
    echo "PASS: reduce around a1 path overlaps"
else
    fails=$((fails + 1))
    echo "FAIL: reduce around a1 path overlaps field"
fi
"$bandagepath" info tmp/component.gfa 1> tmp/std_out 2> tmp/std_err
info_exit=$?
if [[ $info_exit == 0 && ! -s tmp/std_err ]]; then
    passes=$((passes + 1))
    echo "PASS: reload reduced component"
else
    fails=$((fails + 1))
    echo "FAIL: reload reduced component"
    echo "   exit: $info_exit"
    echo "   stderr: $(cat tmp/std_err)"
fi
rm -f tmp/std_out tmp/std_err

test_all "$bandagepath reduce inputs/test_components.gfa tmp/component.gfa --scope aroundcomponent --path PATH_A,PATH_B" 0 "" ""
check_gfa_names tmp/component.gfa S "a1 a2 a3 b1 b2" "c1"
check_gfa_names tmp/component.gfa P "PATH_A PATH_B" ""
check_gfa_names tmp/component.gfa W "WALK_A WALK_B" "" 4

test_all "$bandagepath reduce inputs/test_components.gfa tmp/component.gfa --scope aroundcomponent --walk WALK_A" 0 "" ""
check_gfa_names tmp/component.gfa S "a1 a2 a3" "b1 b2 c1"
check_gfa_names tmp/component.gfa P "PATH_A" "PATH_B"
check_gfa_names tmp/component.gfa W "WALK_A" "WALK_B" 4

test_all "$bandagepath reduce inputs/test_components.gfa tmp/component.gfa --scope aroundnodes --nodes a1 --distance 0" 0 "" ""
check_gfa_names tmp/component.gfa S "a1" "a2 a3 b1 b2 c1"
check_gfa_names tmp/component.gfa P "" "PATH_A PATH_B"
check_gfa_names tmp/component.gfa W "" "WALK_A WALK_B" 4
rm tmp/component.gfa

# BandageNG load tests
test_all "$bandagepath load abc.fastg" 105 "" "<graph>: File does not exist: abc.fastg Run with --help or --helpall for more information."
test_all "$bandagepath load inputs/test.fastg --query abc.fasta" 105 "" "--query: File does not exist: abc.fasta Run with --help or --helpall for more information."

# BandageNG help tests
test_exit_code "$bandagepath --help" 0
test_exit_code "$bandagepath --helpall" 0
test_exit_code "$bandagepath --version" 0

# BandageNG incorrect settings tests
test_all "$bandagepath --abc" 109 "" "The following argument was not expected: --abc Run with --help or --helpall for more information."
test_all "$bandagepath --scope" 114 "" "--scope: 1 required SCOPE:value in {entire->0,aroundnodes->1,aroundhits->4,aroundblast->4,depthrange->5,aroundcomponent->6} OR {0,1,4,4,5,6} missing Run with --help or --helpall for more information."
test_all "$bandagepath --scope abc" 105 "" "--scope: Check abc value in {entire->0,aroundnodes->1,aroundhits->4,aroundblast->4,depthrange->5,aroundcomponent->6} OR {0,1,4,4,5,6} FAILED Run with --help or --helpall for more information."
test_all "$bandagepath --nodes" 114 "" "--nodes: 1 required TEXT missing Run with --help or --helpall for more information."
test_all "$bandagepath --scope aroundcomponent" 105 "" "Bandage-NG error: At least one of --nodes, --path or --walk must be given when the aroundcomponent scope is used. Run with --help or --helpall for more information."
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

# Legacy graph-search option regression (old flag names must keep working)
info_tsv_out="inputs/test.gfa 17 16 60 60 30959 29939 10 29.4118% 1 30959 0 2060 119 2001 2060 2060 2060 532.042 25939"
test_all "$bandagepath info inputs/test.gfa --tsv --blasthits --blastp x" 0 "$info_tsv_out" ""
test_all "$bandagepath info inputs/test.gfa --tsv --scope aroundblast --query inputs/test_queries1.fasta" 0 "$info_tsv_out" ""
test_all "$bandagepath info inputs/test.gfa --tsv --scope aroundblast --query inputs/test_queries1.fasta --blasthits --blastp x" 0 "$info_tsv_out" ""
test_all "$bandagepath --scope aroundblast" 105 "" "Bandage-NG error: A query file must be given with the --query option when the aroundhits scope is used. Run with --help or --helpall for more information."
test_all "$bandagepath --searcher minimap2 --blasthits" 105 "" "Bandage-NG error: --blasthits, --blastp, and --scope aroundblast require the BLAST searcher (use --searcher blast or omit --searcher). Run with --help or --helpall for more information."
test_all "$bandagepath --searcher minimap2 --blastp x" 105 "" "Bandage-NG error: --blasthits, --blastp, and --scope aroundblast require the BLAST searcher (use --searcher blast or omit --searcher). Run with --help or --helpall for more information."
test_all "$bandagepath --searcher minimap2 --scope aroundblast --query inputs/test_queries1.fasta" 105 "" "Bandage-NG error: --blasthits, --blastp, and --scope aroundblast require the BLAST searcher (use --searcher blast or omit --searcher). Run with --help or --helpall for more information."
test_all "$bandagepath --blastp x --searchp y" 105 "" "Bandage-NG error: --blastp and --searchp cannot be used together. Run with --help or --helpall for more information."

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
test_all "$bandagepath --minpatcov" 114 "" "--minpatcov: 1 required FLOAT:FLOAT in [0.15 - 1] missing Run with --help or --helpall for more information."
test_all "$bandagepath --minhitcov" 114 "" "--minhitcov: 1 required FLOAT:FLOAT in [0.15 - 1], or \"off\" missing Run with --help or --helpall for more information."
test_all "$bandagepath --minmeanid" 114 "" "--minmeanid: 1 required FLOAT:FLOAT in [0 - 1], or \"off\" missing Run with --help or --helpall for more information."
test_all "$bandagepath --minpatlen" 114 "" "--minpatlen: 1 required FLOAT:FLOAT in [0 - 10000], or \"off\" missing Run with --help or --helpall for more information."
test_all "$bandagepath --maxpatlen" 114 "" "--maxpatlen: 1 required FLOAT:FLOAT in [0 - 10000], or \"off\" missing Run with --help or --helpall for more information."
test_all "$bandagepath --minlendis" 114 "" "--minlendis: 1 required INT:INT in [-1000000 - 1000000], or \"off\" missing Run with --help or --helpall for more information."
test_all "$bandagepath --maxlendis" 114 "" "--maxlendis: 1 required INT:INT in [-1000000 - 1000000], or \"off\" missing Run with --help or --helpall for more information."
test_all "$bandagepath --maxevprod" 114 "" "--maxevprod: 1 required SCINOT:FLOAT in [1e-999 - 99], or \"off\" missing Run with --help or --helpall for more information."

# querypaths uses the active --searcher (no longer BLAST-gated at CLI parse)
test_all "$bandagepath querypaths --searcher minimap2" 106 "" "<graph> is required Run with --help or --helpall for more information."

check_querypaths_tsv() {
    label=$1
    shift
    rm -f tmp/qp.tsv
    "$@" 1> tmp/std_out 2> tmp/std_err
    exit_code=$?
    rows=0
    if [[ -f tmp/qp.tsv ]]; then
        rows=$(wc -l < tmp/qp.tsv | tr -d ' ')
    fi
    header=""
    if [[ -f tmp/qp.tsv ]]; then
        header=$(head -n 1 tmp/qp.tsv)
    fi
    if [[ $exit_code == 0 && $header == Query* && $rows -ge 2 ]]; then
        passes=$((passes + 1))
        echo "PASS: querypaths $label ($rows rows)"
    else
        fails=$((fails + 1))
        echo "FAIL: querypaths $label"
        echo "   exit: $exit_code rows: $rows"
        echo "   header: $header"
        echo "   stderr: $(cat tmp/std_err)"
    fi
    rm -f tmp/std_out tmp/std_err tmp/qp.tsv
}

check_querypaths_tsv blast "$bandagepath" querypaths inputs/test_query_paths.gfa inputs/test_query_paths.fasta tmp/qp

if ! hmmbuild --dna tmp/nucl.hmm <(printf '# STOCKHOLM 1.0\n#=GF ID nuclq\nnuclq\tATGAAAACCCTGCTGATCCTGGCAGCAGCAGCAGCAAAAG\n//\n') >/dev/null; then
    fails=$((fails + 1))
    echo "FAIL: hmmbuild"
else
    passes=$((passes + 1))
    echo "PASS: hmmbuild"
fi
check_querypaths_tsv minimap2 "$bandagepath" querypaths inputs/test_search.gfa inputs/test_search_nucl.fasta tmp/qp --searcher minimap2 --searchp="-x sr --for-only"
check_querypaths_tsv hmmer "$bandagepath" querypaths inputs/test_search.gfa tmp/nucl.hmm tmp/qp --searcher hmmer --maxevprod off
rm -f tmp/nucl.hmm

rmdir tmp

echo ""
echo "$passes tests passed, $fails tests failed"
if [[ $fails != 0 ]]; then
    exit 1;
fi
