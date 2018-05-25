#!/bin/bash

network='live'

print_usage() {
	echo 'build.sh [-h] [-n {live|beta|test}]'
}

while getopts 'hn:' OPT; do
	case "${OPT}" in
		h)
			print_usage
			exit 0
			;;
		n)
			network="${OPTARG}"
			;;
		*)
			print_usage >&2
			exit 1
			;;
	esac
done

case "${network}" in
	live)
		network_tag=''
		;;
	test|beta)
		network_tag="-${network}"
		;;
	*)
		echo "Invalid network: ${network}" >&2
		exit 1
		;;
esac

REPO_ROOT=`git rev-parse --show-toplevel`
GIT_COMMIT=`git rev-parse HEAD | cut -c-3`
pushd $REPO_ROOT
docker build --build-arg NETWORK="${network}" --build-arg GIT_COMMIT="${GIT_COMMIT}" -f docker/node/Dockerfile -t raiblocks-node${network_tag}:latest .
popd
