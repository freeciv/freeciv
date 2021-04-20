#!/bin/bash

# the revision from the old branch to pick if no revision is specified
DEFAULT_REVISION="dbc31c25"

# the branch of the previous version
PREVIOUS_BRANCH="S3_0"

set -e

cd $(dirname $0)

# list of rulesets to copy
rulesets=`cat ruleset_list.txt`

if test "x$1" = "x" ; then
  REVISION=$DEFAULT_REVISION
else
  REVISION=$1
fi

# sanity check
git merge-base --is-ancestor $REVISION $PREVIOUS_BRANCH

tmpdir="tmp"
tmpindex=".old_data_dir.index"

git read-tree \
 --prefix=tests/rs_test_res/${tmpdir} \
 --index-output=${tmpindex} \
 "${REVISION}:data"
mkdir -p ${tmpdir} && cd ${tmpdir} \
 && (GIT_INDEX_FILE=${tmpindex} git checkout-index -af) \
 && cd .. && rm "../../${tmpindex}"

rm -rf upgrade_rulesets && mkdir upgrade_rulesets
rm -f upgrade_ruleset_list.txt
for ruleset in $rulesets ; do
  if test -d "${tmpdir}/${ruleset}" ; then
    cp -R "${tmpdir}/${ruleset}" "upgrade_rulesets/old_${ruleset}"
    echo "old_${ruleset}" >> upgrade_ruleset_list.txt
  fi
done

rm -r ${tmpdir}

DESCRIPTION=`../../scripts/revision_describer.bash ${REVISION} ../../data`
echo "Most recent sync from $PREVIOUS_BRANCH" > upgrade_ruleset_version.txt
echo "$DESCRIPTION" >> upgrade_ruleset_version.txt
