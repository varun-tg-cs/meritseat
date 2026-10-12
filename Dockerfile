FROM gcc:14-bookworm AS build
WORKDIR /build
COPY src/main.c src/main.c
RUN gcc -std=c11 -O2 -Wall -Wextra -Wpedantic src/main.c -o admission

FROM debian:bookworm-slim
WORKDIR /app
COPY --from=build /build/admission ./admission
COPY web/ ./web/
COPY deploy/start.sh ./start.sh
RUN chmod +x ./start.sh
EXPOSE 10000
CMD ["./start.sh"]
