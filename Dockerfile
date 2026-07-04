FROM alpine:3.23 AS build

RUN apk add --no-cache binutils make musl-dev gcc wayland wayland-static wayland-dev libffi-dev
COPY --exclude=build/ . .
RUN make CFLAGS_EXTRA='-static -lffi'

FROM scratch

COPY --from=build build/zzzclip zzzclip-static
