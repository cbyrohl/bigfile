import h5py
import bigfile
import logging
import argparse

ap = argparse.ArgumentParser('h5tobigfile')
ap.add_argument("hdf5")
ap.add_argument("bigfile")
ap.add_argument("--verify", action='store_true', default=False)
ap.add_argument("--include", action="append")
ap.add_argument("--exclude", action="append")

def traverse(hobj, operation, prefix=""):
    if hasattr(hobj, 'keys'):
        for k in hobj.keys():
            path = prefix + '/' + k
            traverse(hobj[k], operation, path)

    if hasattr(hobj, 'dtype'):
        if hobj.dtype.names:
            for field in hobj.dtype.names:
                path = prefix + field + '/'
                traverse(hobj[field], operation, path)
        else:
            operation(hobj, prefix)

def main(ns):

    hin = h5py.File(ns.hdf5, mode='r')
    bout = bigfile.BigFile(ns.bigfile, create=True)

    def create(hobj, path):
        print("Copying %s as %s" % (str(hobj), path))
        if hobj.dtype.shape and len(hobj.dtype.shape) > 1:
            raise TypeError("bigfile cannot handle tensor dtype : %s" % str(hobj.dtype))

        bb = bout.create(path, hobj.dtype, size=hobj.size)
        bb.attrs['ndarray.shape'] = hobj.shape
        bb.write(0, hobj[:].ravel().copy())

    traverse(hin, create)

    bout.close()

    if ns.verify:
        bout = bigfile.BigFile(ns.bigfile, create=False)
        def verify(hobj, path):
            print("Verifying %s against %s" % (str(hobj), path))
            bb = bout[path]
            assert (hobj.dtype == bb.dtype)
            assert (hobj.shape == bb.attrs['ndarray.shape']).all()
            assert (hobj[:].flat == bb[:]).all()
        traverse(hin, verify)
        bout.close()

    hin.close()
if __name__ == "__main__" :
    ns = ap.parse_args()

    main(ns)