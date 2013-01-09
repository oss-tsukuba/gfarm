/*
 * Created on 2003/06/20
 *
 * To change the template for this generated file go to
 * Window>Preferences>Java>Code Generation>Code and Comments
 */
package gmonitor.logdata;

import java.io.IOException;
import java.util.ArrayList;

/**
 * @author hkondo
 *
 * To change the template for this generated type comment go to
 * Window>Preferences>Java>Code Generation>Code and Comments
 */
public class CachedSeekableFile implements SeekableFile {
	SeekableFile file;
	int CACHE_CAPACITY = 100;
	ArrayList cache = new ArrayList();

	class CacheBlock {
		public static final int BLKSZ = 8192;
		int blkpos = 0;
		int size = 0;
		byte[] data = new byte[BLKSZ];

		public CacheBlock(int bpos, byte[] buf, int sz) {
			if (sz > BLKSZ) {
				throw new ArrayStoreException("Too large data to store into cache block.");
			}
			blkpos = bpos;
			size = sz;
			System.arraycopy(data, 0, buf, 0, size);
		}

		public int getBlockPos() {
			return blkpos;
		}
		public int getBlock(byte[] buf, int idx) {
			if (buf.length < size) {
				throw new ArrayIndexOutOfBoundsException("Too small Buffer to store cached data.");
			}
			//			System.arraycopy(buf, idx, data, 0, size);
			System.arraycopy(data, 0, buf, idx, size);
			return size;
		}
	}

	public CachedSeekableFile(SeekableFile f) {
		file = f;
		cache.ensureCapacity(CACHE_CAPACITY);
	}

	/* (non-Javadoc)
	 * @see gmonitor.logdata.SeekableFile#close()
	 */
	public void close() throws IOException {
		file.close();
		for (int i = 0; i < cache.size(); i++) {
			CacheBlock b = (CacheBlock) cache.get(i);
			b.data = null;
		}
		cache.clear();
	}

	/* (non-Javadoc)
	 * @see gmonitor.logdata.SeekableFile#length()
	 */
	public long size() throws IOException {
		return file.size();
	}

	/* (non-Javadoc)
	 * @see gmonitor.logdata.SeekableFile#read(byte[], int, int)
	 */
	public int read(byte[] buf, int idx, int amount) throws IOException {
		byte[] internal_buf = new byte[CacheBlock.BLKSZ];
		int transferred_amount = 0;
		synchronized (cache) {
			int blk = (idx / CacheBlock.BLKSZ); // 開始ブロック番号をセットしておく
			// キャッシュの具合によっては、まとめてどかっと読み込んでからキャッシュに振り向けたほうが効率よいかもしれない。			
			do {
				int i = findCachedBlock(blk);
				if (i < 0) {
					// ファイルから読み出してキャッシュとバッファに充填する
					int to_read = amount - transferred_amount;
					if (to_read > CacheBlock.BLKSZ) {
						to_read = CacheBlock.BLKSZ;
					}
					int realsz = file.read(internal_buf, 0, to_read);
					CacheBlock cb = new CacheBlock(blk, internal_buf, realsz);
					cb.getBlock(buf, transferred_amount);
					// キャッシュがあふれそうになったらふるいものを削除する
					cache.add(cb);
					if (cache.size() > CACHE_CAPACITY) {
						// リストの先頭のものほど古いので、一番先頭のものを削除
						CacheBlock c = (CacheBlock) cache.remove(0);
						c.data = null; // 明示的に参照を切っておく
					}
					transferred_amount += realsz;
				} else {
					// キャッシュからバッファに充填する
					CacheBlock cb = (CacheBlock) cache.remove(i);
					transferred_amount += cb.getBlock(buf, transferred_amount);
					cache.add(cb);
				}
				blk++;
			} while (transferred_amount < amount);

			if (transferred_amount != amount) {
				// ASSERT FAILED.
				throw new AssertionError(
					"Caching system is broken. Request is "
						+ amount
						+ " but Response is "
						+ transferred_amount);
			}
		}
		return transferred_amount;
	}

	private int findCachedBlock(int blkpos) {
		for (int i = 0; i < cache.size(); i++) {
			CacheBlock cb = (CacheBlock) cache.get(i);
			if (cb.getBlockPos() == blkpos) {
				return i;
			}
		}
		return -1;
	}

	/* (non-Javadoc)
	 * @see gmonitor.logdata.SeekableFile#seek(long)
	 */
	public void seek(long pos) throws IOException {
		file.seek(pos);
	}

}
