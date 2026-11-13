/**
 * The fetch backend's JS half (emscripten's src/lib/libwasmfs_fetch.js, MIT), with a bounded cache: the game
 * reads data.zip from the server over the network (src/storage.cpp, tools/web/serve.py --data), a range at a
 * time, and emscripten's own keeps every range it fetched for as long as the page lives, which over a session
 * would be gigabytes on a phone. Here the least recently read chunks go once the cache passes its limit.
 * Linked with --js-library after emscripten's libraries, so this definition replaces theirs.
 */

addToLibrary({
  $ctFetchFiles: {},
  $ctFetchCache: {
    bytes: 0,
    limit: 192 * 1024 * 1024,
    ahead: 128 * 1024,
    // How far a request reaches when its file is being read straight through.
    aheadMost: 1024 * 1024,
    order: new Map(),
    // What the requests since the last report cost, for the log.
    stats: {since: 0, requests: 0, bytes: 0, waited: 0, worst: 0},
  },

  _wasmfs_create_fetch_backend_js__deps: [
    '$wasmFS$backends',
    '$ctFetchFiles',
    '$ctFetchCache',
    '_wasmfs_fetch_get_file_url',
    '_wasmfs_fetch_get_chunk_size',
  ],
  _wasmfs_create_fetch_backend_js: async function(backend) {
    function url(file) {
      var fileUrl = UTF8ToString(__wasmfs_fetch_get_file_url(file));
      try {
        return new URL(fileUrl, self.location.href).toString();
      } catch (_e) {
        throw {status: 404};
      }
    }

    // The file's size and chunks, asking the server once: one that answers ranges is read a chunk at a time;
    // any other, or a small file, is fetched whole and kept (data.json).
    async function info(file) {
      var known = ctFetchFiles[file];
      if (known) {
        return known;
      }
      var address = url(file);
      var chunkSize = __wasmfs_fetch_get_chunk_size(file);
      var head = await fetch(address, {method: 'HEAD', headers: {'Range': 'bytes=0-'}});
      var length = parseInt(head.headers.get('Content-Length') ?? '', 10);
      if (head.ok && head.headers.get('Accept-Ranges') == 'bytes' && length > chunkSize * 2) {
        known = {url: address, size: length, chunkSize: chunkSize, chunks: new Map(), whole: null,
                 next: -1, ahead: ctFetchCache.ahead};
      } else {
        var response = await fetch(address);
        if (!response.ok) {
          throw response;
        }
        var whole = new Uint8Array(await response.arrayBuffer());
        known = {url: address, size: whole.byteLength, chunkSize: whole.byteLength || 1, chunks: new Map(), whole: whole};
      }
      ctFetchFiles[file] = known;
      return known;
    }

    function touch(file, index, chunk) {
      var key = file + ':' + index;
      ctFetchCache.order.delete(key);
      ctFetchCache.order.set(key, [file, index, chunk.byteLength]);
    }

    // Drops the least recently read chunks while the cache is over its limit.
    function trim() {
      for (var [key, [file, index, length]] of ctFetchCache.order) {
        if (ctFetchCache.bytes <= ctFetchCache.limit) {
          break;
        }
        ctFetchCache.order.delete(key);
        ctFetchFiles[file]?.chunks.delete(index);
        ctFetchCache.bytes -= length;
      }
    }

    // Whether every chunk over [offset, offset + length) is present.
    function present(known, offset, length) {
      var size = known.chunkSize;
      for (var i = Math.floor(offset / size); i <= Math.floor((offset + length - 1) / size); i++) {
        if (!known.chunks.has(i)) {
          return false;
        }
      }
      return true;
    }

    // One line in the log for each five seconds with requests in them: how many, how much, and how long the
    // game waited for them, which says whether a slow load is the network's or the device's.
    function count(began, bytes) {
      var stats = ctFetchCache.stats;
      var took = performance.now() - began;
      if (stats.requests == 0) {
        stats.since = began;
        setTimeout(() => {
          out(`fetch: ${stats.requests} requests, ${(stats.bytes / 1048576).toFixed(1)} MiB, waited ` +
              `${(stats.waited / 1000).toFixed(1)} s of ${((performance.now() - stats.since) / 1000).toFixed(1)} s, ` +
              `longest ${Math.round(stats.worst)} ms`);
          stats.requests = stats.bytes = stats.waited = stats.worst = 0;
        }, 5000);
      }
      stats.requests++;
      stats.bytes += bytes;
      stats.waited += took;
      stats.worst = Math.max(stats.worst, took);
    }

    // Makes the chunks over [offset, offset + length) present, in one request for those missing.
    async function load(file, known, offset, length) {
      var size = known.chunkSize;
      var first = Math.floor(offset / size);
      var last = Math.floor((offset + length - 1) / size);
      var from = -1;
      var to = -1;
      for (var i = first; i <= last; i++) {
        if (!known.chunks.has(i)) {
          if (from < 0) {
            from = i;
          }
          to = i;
        }
      }
      if (from < 0) {
        return;
      }
      // A request reaches a little past its read: the archive is read an entry's header first (a few dozen
      // bytes), then its data right after, and for a small file this one request then answers both. A read that
      // goes on from where the last one ended is a large file read straight through: each of its requests
      // reaches twice as far as the one before, so a file of megabytes is a few requests, not dozens, and a
      // network slow to answer each one is felt less.
      known.ahead = offset == known.next ? Math.min(known.ahead * 2, ctFetchCache.aheadMost) : ctFetchCache.ahead;
      var reach = Math.min(from + Math.ceil(known.ahead / size) - 1, Math.floor((known.size - 1) / size));
      while (to < reach && !known.chunks.has(to + 1)) {
        to++;
      }
      var start = from * size;
      var end = Math.min((to + 1) * size, known.size);
      var began = performance.now();
      var response = await fetch(known.url, {headers: {'Range': `bytes=${start}-${end - 1}`}});
      if (!response.ok) {
        throw response;
      }
      var bytes = new Uint8Array(await response.arrayBuffer());
      count(began, bytes.byteLength);
      if (response.status != 206 && bytes.byteLength == known.size) {
        bytes = bytes.subarray(start, end);
      }
      for (var j = from; j <= to; j++) {
        if (!known.chunks.has(j)) {
          var chunk = bytes.slice(j * size - start, Math.min((j + 1) * size, end) - start);
          known.chunks.set(j, chunk);
          ctFetchCache.bytes += chunk.byteLength;
          touch(file, j, chunk);
        }
      }
    }

    wasmFS$backends[backend] = {
      allocFile: async (file) => {},
      freeFile: async (file) => {
        var known = ctFetchFiles[file];
        if (known) {
          for (var [index, chunk] of known.chunks) {
            ctFetchCache.order.delete(file + ':' + index);
            ctFetchCache.bytes -= chunk.byteLength;
          }
          delete ctFetchFiles[file];
        }
      },
      write: async (file, buffer, length, offset) => {
        return -{{{ cDefs.EROFS }}};
      },
      read: async (file, buffer, length, offset) => {
        // off_t arrives as a BigInt; the buffer may lie past 2 GiB, where a pointer reads as negative.
        offset = Number(offset);
        buffer >>>= 0;
        length >>>= 0;
        if (offset < 0 || length <= 0) {
          return 0;
        }
        var known;
        try {
          known = await info(file);
          length = Math.min(length, known.size - offset);
          if (length <= 0) {
            return 0;
          }
          if (known.whole) {
            HEAPU8.set(known.whole.subarray(offset, offset + length), buffer);
            return length;
          }
          // Another read may trim chunks this one found present while its own request was out: ask again
          // until all are here, then copy without awaiting, so none can go before they are copied.
          do {
            await load(file, known, offset, length);
          } while (!present(known, offset, length));
        } catch (failed) {
          return failed?.status === 404 ? -{{{ cDefs.ENOENT }}} : -{{{ cDefs.EIO }}};
        }
        // From here to the return nothing awaits, so no other read trims the chunks this one copies.
        var size = known.chunkSize;
        var first = Math.floor(offset / size);
        var last = Math.floor((offset + length - 1) / size);
        for (var i = first; i <= last; i++) {
          var chunk = known.chunks.get(i);
          var chunkStart = i * size;
          var start = Math.max(chunkStart, offset);
          var end = Math.min(chunkStart + chunk.byteLength, offset + length);
          HEAPU8.set(chunk.subarray(start - chunkStart, end - chunkStart), buffer + (start - offset));
          touch(file, i, chunk);
        }
        known.next = offset + length;
        trim();
        return length;
      },
      getSize: async (file) => {
        try {
          return (await info(file)).size;
        } catch (_failed) {
          return 0;
        }
      },
    };
  },
});
