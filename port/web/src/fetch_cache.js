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
    // How far a request reaches when its part of the file is being read onward.
    aheadMost: 2 * 1024 * 1024,
    // How far past the last request a read may start and still count as reading onward.
    near: 1024 * 1024,
    // How many parts of a file may be read onward at once (the game's threads each read their own).
    streaks: 4,
    order: new Map(),
    // What the requests since the last report cost, for the log.
    stats: {since: 0, requests: 0, ahead: 0, bytes: 0, waited: 0, worst: 0},
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
      var head = await fetch(address, {method: 'HEAD', headers: {'Range': 'bytes=0-'}, cache: 'no-store'});
      var length = parseInt(head.headers.get('Content-Length') ?? '', 10);
      if (head.ok && head.headers.get('Accept-Ranges') == 'bytes' && length > chunkSize * 2) {
        known = {url: address, size: length, chunkSize: chunkSize, chunks: new Map(), whole: null,
                 streaks: [], flying: []};
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

    // One line in the log for each five seconds with requests in them: how many (and how many of those were
    // sent ahead of the game's reads), how much, and how long the game waited, which says whether a slow load
    // is the network's or the device's.
    function report(began) {
      var stats = ctFetchCache.stats;
      if (stats.since == 0) {
        stats.since = began;
        setTimeout(() => {
          out(`fetch: ${stats.requests} requests (${stats.ahead} ahead), ${(stats.bytes / 1048576).toFixed(1)} MiB, ` +
              `waited ${(stats.waited / 1000).toFixed(1)} s of ${((performance.now() - stats.since) / 1000).toFixed(1)} s, ` +
              `longest ${Math.round(stats.worst)} ms`);
          stats.since = stats.requests = stats.ahead = stats.bytes = stats.waited = stats.worst = 0;
        }, 5000);
      }
      return stats;
    }

    // The request under way that covers chunk index, if one does.
    function flying(known, index) {
      for (var flight of known.flying) {
        if (flight.from <= index && index <= flight.to) {
          return flight;
        }
      }
      return null;
    }

    // Asks the server for chunks [from, to] and keeps them. Not kept by the browser (no-store): its cache gives
    // one address to one request at a time, and every request here is for data.zip.
    function request(file, known, from, to, ahead) {
      var size = known.chunkSize;
      var start = from * size;
      var end = Math.min((to + 1) * size, known.size);
      var flight = {from: from, to: to, ahead: ahead, done: null};
      var stats = report(performance.now());
      stats.requests++;
      stats.ahead += ahead ? 1 : 0;
      flight.done = (async () => {
        try {
          var response = await fetch(known.url, {headers: {'Range': `bytes=${start}-${end - 1}`}, cache: 'no-store'});
          if (!response.ok) {
            throw response;
          }
          var bytes = new Uint8Array(await response.arrayBuffer());
          ctFetchCache.stats.bytes += bytes.byteLength;
          if (response.status != 206 && bytes.byteLength == known.size) {
            bytes = bytes.subarray(start, end);
          }
          if (ctFetchFiles[file] !== known) {
            return;
          }
          for (var j = from; j <= to; j++) {
            if (!known.chunks.has(j)) {
              var chunk = bytes.slice(j * size - start, Math.min((j + 1) * size, end) - start);
              known.chunks.set(j, chunk);
              ctFetchCache.bytes += chunk.byteLength;
              touch(file, j, chunk);
            }
          }
        } finally {
          known.flying.splice(known.flying.indexOf(flight), 1);
        }
      })();
      known.flying.push(flight);
      return flight.done;
    }

    // The part of the file being read onward that chunk index belongs to: one whose last request ended at or a
    // little before it.
    function streakAt(known, index) {
      var near = Math.ceil(ctFetchCache.near / known.chunkSize);
      for (var streak of known.streaks) {
        if (index >= streak.end - streak.reach && index <= streak.end + near) {
          return streak;
        }
      }
      return null;
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
          var flight = flying(known, i);
          if (flight) {
            // Already asked for (sent ahead, or another thread's read): wait for that answer, not a second one.
            try {
              await flight.done;
            } catch (_failed) {
            }
            return;
          }
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
      // bytes), then its data right after, and for a small file this one request then answers both. A read
      // that starts where the last request ended, or a little after, is a part of the file being read onward
      // (a large file read straight through, or the first bytes of each texture of a folder, which lie one
      // after another with the pictures between): each such request reaches twice as far as the one before.
      // A phone waits a sixth of a second for every answer however small, so what counts is how few there are.
      var least = Math.ceil(ctFetchCache.ahead / size);
      var streak = streakAt(known, from);
      if (streak) {
        streak.reach = Math.min(streak.reach * 2, Math.ceil(ctFetchCache.aheadMost / size));
      } else {
        streak = {end: 0, reach: least};
        known.streaks.push(streak);
        if (known.streaks.length > ctFetchCache.streaks) {
          known.streaks.shift();
        }
      }
      var reach = Math.min(from + streak.reach - 1, Math.floor((known.size - 1) / size));
      while (to < reach && !known.chunks.has(to + 1) && !flying(known, to + 1)) {
        to++;
      }
      streak.end = to + 1;
      await request(file, known, from, to, false);
    }

    // Once a part of the file is being read onward and a read is past the middle of what was last asked for,
    // asks for the stretch after it without waiting, so the answer is on its way, or here, when the game's
    // reads get there.
    function sendAhead(file, known, last) {
      var size = known.chunkSize;
      var streak = streakAt(known, last);
      if (!streak || streak.reach <= Math.ceil(ctFetchCache.ahead / size) || last >= streak.end ||
          last < streak.end - Math.ceil(streak.reach / 2)) {
        return;
      }
      var from = streak.end;
      var end = Math.floor((known.size - 1) / size);
      if (from > end || known.chunks.has(from) || flying(known, from) || known.flying.some((flight) => flight.ahead)) {
        return;
      }
      streak.reach = Math.min(streak.reach * 2, Math.ceil(ctFetchCache.aheadMost / size));
      var to = from;
      var reach = Math.min(from + streak.reach - 1, end);
      while (to < reach && !known.chunks.has(to + 1) && !flying(known, to + 1)) {
        to++;
      }
      streak.end = to + 1;
      request(file, known, from, to, true).catch(() => {});
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
          if (!present(known, offset, length)) {
            var began = performance.now();
            do {
              await load(file, known, offset, length);
            } while (!present(known, offset, length));
            var stats = report(began);
            var took = performance.now() - began;
            stats.waited += took;
            stats.worst = Math.max(stats.worst, took);
          }
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
        sendAhead(file, known, last);
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
