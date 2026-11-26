FROM debian:bookworm-slim

RUN apt-get update \
    && apt-get install -y --no-install-recommends build-essential libsqlite3-dev ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY . .
RUN make -C backend
RUN chmod +x start-render.sh

ENV PORT=10000
ENV UNISPHERE_SECURE_COOKIE=1
EXPOSE 10000

CMD ["./start-render.sh"]
