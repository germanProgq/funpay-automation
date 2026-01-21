FROM debian:bookworm-slim

ENV DEBIAN_FRONTEND=noninteractive

RUN printf 'Acquire::Retries "5";\nAcquire::https::Timeout "30";\nAcquire::http::Timeout "30";\n' \
    > /etc/apt/apt.conf.d/80-retries
RUN for f in /etc/apt/sources.list /etc/apt/sources.list.d/*.list; do \
      if [ -f "$f" ]; then sed -i 's|http://|https://|g' "$f"; fi; \
    done; \
    true

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    ninja-build \
    pkg-config \
    libgtk-4-dev \
    libcurl4-openssl-dev \
    libxml2-dev \
    libpq-dev \
    libsqlite3-dev \
    entr \
    ca-certificates \
  && rm -rf /var/lib/apt/lists/*

WORKDIR /workspace

COPY tools/docker/dev_entrypoint.sh /usr/local/bin/fpv-dev
RUN chmod +x /usr/local/bin/fpv-dev

ENTRYPOINT ["/usr/local/bin/fpv-dev"]
