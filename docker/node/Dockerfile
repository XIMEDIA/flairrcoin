FROM ubuntu:16.04

ARG NETWORK=live

ENV BOOST_ROOT=/tmp/boost_install

ADD ci /tmp/ci

RUN apt-get update -qq && apt-get install -yqq \
    build-essential \
    cmake \
    g++ \
    wget \
    libssl-dev && \
    /tmp/ci/bootstrap_boost.sh -m

ADD ./ /tmp/src

ARG GIT_COMMIT
ENV GIT_COMMIT ${GIT_COMMIT}
RUN mkdir /tmp/build && \
    cd /tmp/build && \
    cmake /tmp/src \
        -DDISABLE_NATIVE_ARCH=ON \
        -DRAIBLOCKS_SECURE_RPC=ON \
        -DBOOST_ROOT=${BOOST_ROOT} \
        -DACTIVE_NETWORK=rai_${NETWORK}_network \
        -DGIT_COMMIT=${GIT_COMMIT} && \
    cmake --build . --config Release --target rai_node -- -j && \
    cd .. && \
    echo ${NETWORK} > /etc/nano-network

FROM ubuntu:16.04
COPY --from=0 /tmp/build/rai_node /usr/bin
COPY --from=0 /etc/nano-network /etc
COPY docker/node/entry.sh /entry.sh
COPY docker/node/config /usr/share/raiblocks/config
RUN chmod +x /entry.sh
CMD ["/bin/bash",  "/entry.sh"]
