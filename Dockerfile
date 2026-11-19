# Build a static binary, index the sample corpus, and serve it.
FROM gcc:13 AS build
WORKDIR /src
COPY . .
RUN make LDFLAGS="-pthread -static" build/searchx \
 && ./build/searchx index data/sample.tsv /sample.idx 1

FROM scratch
COPY --from=build /src/build/searchx /searchx
COPY --from=build /sample.idx /data/index.idx
EXPOSE 8080
# Mount your own index at /data/index.idx to serve a bigger corpus.
ENTRYPOINT ["/searchx", "serve", "/data/index.idx", "8080", "4", "0.0.0.0"]
