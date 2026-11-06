========
Cookbook
========

Short recipes for common tasks. They use two hours of Cluster 1 magnetic field data, downloaded
from the Cluster Science Archive with the function of the :doc:`quickstart`:

.. code-block:: python

    import io
    import tarfile
    import urllib.request

    import numpy as np
    import cefast

    def fetch_csa(dataset, start, stop):
        """Download one CEF file from the Cluster Science Archive."""
        url = ("https://csa.esac.esa.int/csa-sl-tap/data?RETRIEVAL_TYPE=product"
               f"&DATASET_ID={dataset}&START_DATE={start}&END_DATE={stop}"
               "&DELIVERY_FORMAT=CEF&DELIVERY_INTERVAL=all")
        with urllib.request.urlopen(url) as response:
            archive = tarfile.open(fileobj=io.BytesIO(response.read()))
        member = next(m for m in archive if m.name.endswith(".cef"))
        return member.name.rsplit("/", 1)[-1], archive.extractfile(member).read()

    name, content = fetch_csa("C1_CP_FGM_SPIN",
                              "2005-06-10T00:00:00Z", "2005-06-10T02:00:00Z")
    f = cefast.load(content)

Replace fill values with NaN
============================

Numeric values keep the file's ``FILLVAL``. Replace them with ``NaN`` to get a float array
you can average and plot:

.. code-block:: python

    def with_nan(variable):
        raw = variable.values
        values = raw.astype(np.float64)
        fill = variable.attributes.get("FILLVAL")
        if isinstance(fill, (int, float)):
            values[raw == raw.dtype.type(fill)] = np.nan
        return values

    B = with_nan(f["B_vec_xyz_gse__C1_CP_FGM_SPIN"])
    print(np.nanmean(B, axis=0))

``astype`` makes a copy, so the data inside the file stays as it was. The comparison is done in
the variable's own type: a ``float32`` fill value converted to ``float64`` is not equal to the
``float64`` number ``-1e30``.

Find the data variables
=======================

Archive files mark their variables with ``PARAMETER_TYPE``: ``Data`` for measurements,
``Support_Data`` for times, coordinates and instrument settings.

.. code-block:: python

    data = [name for name in f if f[name].attributes.get("PARAMETER_TYPE") == "Data"]
    print(data)

Make a pandas DataFrame
=======================

One column per component, indexed by time:

.. code-block:: python

    import pandas as pd

    B = f["B_vec_xyz_gse__C1_CP_FGM_SPIN"]
    df = pd.DataFrame(B.values,
                      index=f[B.attributes["DEPEND_0"]].values,
                      columns=B.attributes["LABEL_1"])
    print(df.head())

Make an xarray Dataset
======================

Every record-varying variable that shares the same time goes into one Dataset. ``DEPEND_0``
gives the time axis:

.. code-block:: python

    import xarray as xr

    def to_xarray(f):
        """A Dataset of every record-varying variable that has a DEPEND_0 time."""
        variables = {}
        for name in f:
            variable = f[name]
            time = variable.attributes.get("DEPEND_0")
            if variable.is_nrv or time is None:
                continue
            values = variable.values
            dims = ["time"] + [f"{name}_dim{i}" for i in range(1, values.ndim)]
            variables[name] = xr.DataArray(values, dims=dims,
                                           coords={"time": f[time].values},
                                           attrs=variable.attributes)
        return xr.Dataset(variables, attrs=f.attributes)

    ds = to_xarray(f)
    print(ds["B_mag__C1_CP_FGM_SPIN"].mean().item())

``attrs=f.attributes`` keeps the metadata. Some attribute values are times or lists: convert
them to text before you write the Dataset to netCDF.

Read several files
==================

Load each file, then join the arrays along the record axis:

.. code-block:: python

    files = [fetch_csa("C1_CP_FGM_SPIN", start, stop)[1]
             for start, stop in [("2005-06-10T00:00:00Z", "2005-06-10T01:00:00Z"),
                                 ("2005-06-10T01:00:00Z", "2005-06-10T02:00:00Z")]]
    loaded = [cefast.load(content) for content in files]

    time = np.concatenate([g["time_tags__C1_CP_FGM_SPIN"].values for g in loaded])
    B = np.concatenate([g["B_vec_xyz_gse__C1_CP_FGM_SPIN"].values for g in loaded])
    print(time.shape, B.shape)

Loading releases the GIL, so a thread pool reads many files in parallel. Pass ``threads=1``
then, so each file does not also start its own threads:

.. code-block:: python
    :class: no-playground

    from concurrent.futures import ThreadPoolExecutor

    with ThreadPoolExecutor() as pool:
        loaded = list(pool.map(lambda content: cefast.load(content, threads=1), files))

Plot a spectrogram
==================

Particle data have more dimensions. This PEACE electron dataset gives, for each time, a flux
per pitch angle (``DEPEND_1``) and energy (``DEPEND_2``):

.. code-block:: python

    import matplotlib.pyplot as plt
    from matplotlib.colors import LogNorm

    name, content = fetch_csa("C2_CP_PEA_PITCH_FIX_DEFlux",
                              "2005-06-10T00:00:00Z", "2005-06-10T00:10:00Z")
    peace = cefast.load(content)
    flux = peace["Data__C2_CP_PEA_PITCH_FIX_DEFlux"]
    print(flux.values.shape)   # (records, 12 pitch angles, 100 energies)

    # Average over pitch angles, skipping fill values; a bin with no data stays empty.
    spectrum = np.ma.masked_invalid(with_nan(flux)).mean(axis=1)
    time = peace[flux.attributes["DEPEND_0"]].values
    energy = peace[flux.attributes["DEPEND_2"]].values

    plt.pcolormesh(time, energy, spectrum.T, norm=LogNorm(), shading="nearest")
    plt.yscale("log")
    plt.ylabel(f"Energy ({peace[flux.attributes['DEPEND_2']].attributes['UNITS']})")
    plt.colorbar(label=flux.attributes["UNITS"])
    plt.show()
