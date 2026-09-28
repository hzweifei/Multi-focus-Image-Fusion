"""Multi-focus fusion. Color arrays follow OpenCV's BGR convention."""
import numpy as np
from . import _mif
from ._mif import Alignment, FocusMeasure, FusionMethod, FusionOptions

__all__ = ["Alignment", "FocusMeasure", "FusionMethod", "FusionOptions", "fuse", "fuse_detailed"]
__version__ = "0.1.0"


def _prepare(images):
    prepared = []
    for image in images:
        if not isinstance(image, np.ndarray):
            raise TypeError("Each image must be a NumPy array")
        if image.dtype not in (np.dtype("uint8"), np.dtype("uint16"), np.dtype("float32")):
            raise ValueError("Images must have native uint8, uint16 or float32 dtype")
        if image.ndim != 2 and not (image.ndim == 3 and image.shape[2] == 3):
            raise ValueError("Expected H x W grayscale or H x W x 3 BGR arrays")
        prepared.append(np.ascontiguousarray(image))
    return prepared


def fuse(images, options=None):
    """Fuse 2+ equally shaped/dtyped arrays. Float32 values must be in [0, 1].

    Non-contiguous and read-only arrays are accepted. Inputs are copied before
    the C++ processing releases the GIL. Output owns its storage; uint16 depth
    is preserved. Alignment crops to the common valid image rectangle.
    """
    return _mif.fuse(_prepare(images), options if options is not None else FusionOptions())


def fuse_detailed(images, options=None):
    """Return image, int32 focus_indices, crop, transforms and optional weights.

    crop is (x, y, width, height) in the first input's coordinates. Each 2x3
    transform maps reference coordinates to the corresponding source image.
    Set options.keep_weight_maps = True to return normalized detail weights.
    """
    return _mif.fuse_detailed(_prepare(images), options if options is not None else FusionOptions())

