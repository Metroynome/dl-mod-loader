FROM dnawrkshp/ps2dev-libdl:latest

# Pinned defaults live in docker-init.sh; build arguments can override them.
ARG LIBRAC4_REF
ARG LIBDL_REF
ENV LIBRAC4_DIR=/opt/dl-mod-loader-deps/librac4 \
    LIBDL_DIR=/opt/dl-mod-loader-deps/libdl
COPY docker-init.sh /usr/local/bin/dl-docker-init
RUN sh /usr/local/bin/dl-docker-init

WORKDIR /workspace
# Mount the repository here so generated modules and release files stay on host.
CMD ["make", "all"]
