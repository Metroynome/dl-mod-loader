FROM dnawrkshp/ps2dev-libdl:latest

# Pin the library revisions used to validate the loader; override to upgrade.
ARG LIBRAC4_REF=56c3b26c16436986ab6502ee50724216f518e591
ARG LIBDL_REF=3cb909efb6379ea23c0ab6f964ff62f70b63a579
ENV LIBRAC4_DIR=/opt/dl-mod-loader-deps/librac4 \
    LIBDL_DIR=/opt/dl-mod-loader-deps/libdl
COPY docker-init.sh /usr/local/bin/dl-docker-init
RUN sh /usr/local/bin/dl-docker-init

WORKDIR /workspace
# Mount the repository here so generated modules and release files stay on host.
CMD ["make", "all"]
