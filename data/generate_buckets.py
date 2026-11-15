import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
from pathlib import Path

from concurrent.futures import ThreadPoolExecutor
import kmedoids
import time

from plot_equity_histograms import get_cvs_files

def get_bin_labels(
        csvFile: Path
):
    columns = pd.read_csv(csvFile, nrows = 0). columns
    return sorted(
        (col for col in columns if col.startswith("bin_")),
        key = lambda column: int(column.removeprefix("bin_"))
    )

def iter_histograms_in_chunks(
        csvFiles: list[Path],
        chunksize: int
):
    # bin labels
    bins = get_bin_labels(csvFiles[0])

    # iterate over each csv files
    for file in csvFiles:
        # chunk out a set of histograms
        histogramChunks = pd.read_csv(
            file,
            usecols = bins,
            chunksize = chunksize)
        # return the chunk for use
        for chunk in histogramChunks:
            yield chunk[bins].to_numpy(dtype = np.float32) # recall function once next chunk required

def histogram_sampling(
        csvFiles: list[Path],
        samplesize: int,
        chunksize: int,
        seed: int
):
    bins = get_bin_labels(csvFiles[0])
    randGen = np.random.default_rng(seed)
    sampleHistograms = np.empty((0, len(bins)), dtype = np.float32)
    sampleIndexes = np.empty(0, dtype = np.int64)
    sampleRandKeys = np.empty(0, dtype = np.float64)
    chunkIndexOffset = 0

    for histogramsChunk in iter_histograms_in_chunks(csvFiles, chunksize):
        indexes = chunkIndexOffset + np.arange(len(histogramsChunk), dtype = np.int64)
        chunkIndexOffset += len(histogramsChunk)

        randKeys = randGen.random(len(histogramsChunk))
        availableKeys = np.concatenate((sampleRandKeys, randKeys))
        availableHistograms = np.concatenate((sampleHistograms, histogramsChunk), axis = 0)
        availableIndexes = np.concatenate((sampleIndexes, indexes))

        # subsample from the previous chunk subsample + new chunk
        count = min(samplesize, len(availableKeys)) # size of subsample
        sampleFromAvailable = np.argpartition(availableKeys, -count)[-count:] # the subsample chooses the highest rand keys

        # new subsample values
        sampleRandKeys = availableKeys[sampleFromAvailable]
        sampleHistograms = availableHistograms[sampleFromAvailable]
        sampleIndexes = availableIndexes[sampleFromAvailable]

    return sampleHistograms, sampleIndexes

def compute_emd_distance_matrix(
        histograms: np.ndarray,
        blockSize: int,
        numthreads: int | None = None
):
    numrows, numbins = histograms.shape
    distanceMatrix = np.empty((numrows, numrows), dtype = np.float32)

    # ensure that on conversion to np.float32 values are normalised (if not already that dtype)
    values = np.asarray(histograms, dtype = np.float32)
    normalisation = values.sum(axis = 1, keepdims = True)
    cdfs = np.cumsum(values / normalisation, axis = 1)

    # compute pairwise distances block-by-block
    blocks = range(0, numrows, blockSize)
    blockDistanceMatrixIndices = (
        (
            i_start,
            min(i_start + blockSize, numrows),
            j_start,
            min(j_start + blockSize, numrows)
        )
        for i_start in blocks
        for j_start in range(i_start, numrows, blockSize) # only consider upper triangle to avoid repeated computation
    )

    binwidth = 1 / numbins
    def compute_block_distance_matrix(bounds: tuple[int, int, int, int]):
        i_start, i_end, j_start, j_end = bounds
        # calculate emd
        emd = np.abs(cdfs[i_start:i_end, None, :] - cdfs[None, j_start:j_end, :]).sum(axis = 2) * binwidth

        distanceMatrix[i_start:i_end, j_start:j_end] = emd
        # fills lower diagonal
        if i_start != j_start:
            distanceMatrix[j_start:j_end, i_start:i_end] = emd.T

    # parallelisation of blocks
    with ThreadPoolExecutor(max_workers = numthreads) as executor:
        for _ in executor.map(compute_block_distance_matrix, blockDistanceMatrixIndices):
            pass

    # diagonals of distance matrices is 0
    np.fill_diagonal(distanceMatrix, 0.0)

    return distanceMatrix

def assign_clusters(
        csvFiles: list[Path],
        medoids: np.ndarray,
        chunksize: int,
        outputDir: Path
):
    normalisations = medoids.sum(axis = 1, keepdims = True)
    medoidscdfs = np.cumsum(medoids / normalisations, axis = 1)

    chunkIndexOffset = 0
    temporaryOutput = outputDir.with_name(f"{outputDir.name}.tmp")
    with temporaryOutput.open("w", encoding = "utf-8") as outputFile:
        outputFile.write("index,cluster\n")
        for histogramsChunk in iter_histograms_in_chunks(csvFiles, chunksize):
            indexes = np.arange(chunkIndexOffset, chunkIndexOffset + len(histogramsChunk), dtype = np.int64)

            histograms = np.asanyarray(histogramsChunk, dtype = np.float32)
            normalisations = histograms.sum(axis = 1, keepdims = True)
            cdfs = np.cumsum(histograms / normalisations, axis = 1)

            closestmedoids = np.empty(len(cdfs), dtype = np.int64)
            for rowStart in range(0, len(cdfs), 4_096):
                rowEnd = min(rowStart + 4_096, len(cdfs))
                rowCdfs = cdfs[rowStart:rowEnd]
                bestDistances = np.full(len(rowCdfs), np.inf, dtype = np.float32)
                bestMedoids = np.zeros(len(rowCdfs), dtype = np.int64)

                for medoidStart in range(0, len(medoids), 32):
                    medoidEnd = min(medoidStart + 32, len(medoids))
                    distances = np.abs(
                        rowCdfs[:, None, :] - medoidscdfs[None, medoidStart:medoidEnd, :]
                    ).sum(axis = 2)
                    localMedoids = np.argmin(distances, axis = 1)
                    localDistances = distances[np.arange(len(rowCdfs)), localMedoids]
                    improved = localDistances < bestDistances
                    bestDistances[improved] = localDistances[improved]
                    bestMedoids[improved] = medoidStart + localMedoids[improved]

                closestmedoids[rowStart:rowEnd] = bestMedoids

            np.savetxt(
                outputFile,
                np.column_stack((indexes, closestmedoids)),
                delimiter = ",",
                fmt = "%d"
            )

            chunkIndexOffset += len(histogramsChunk)

    temporaryOutput.replace(outputDir)

def cluster_model(
        street: str,
        numcluster: int = 500,
        samplesize: int = 30_000,
        histogramsChunksize: int = 100_000,
        threadChunksize: int = 512,
        seed: int = 42,
        threadnum: int = 4
):
    csvFiles = get_cvs_files(street)
    outputDir = Path(__file__).resolve().parent / "abstractions"

    # timer
    start = time.perf_counter()

    [sample, sampleIndexes] = histogram_sampling(csvFiles, samplesize, histogramsChunksize, seed)
    sampleDistanceMatrix = compute_emd_distance_matrix(sample, threadChunksize, threadnum)
    model = kmedoids.KMedoids(
        n_clusters = numcluster,
        metric = "precomputed", # computed via emd
        method = "fasterpam",
        random_state = seed
    )
    model.fit_predict(sampleDistanceMatrix)
    medoidsSampleIndexes = model.medoid_indices_
    medoidsIndexes = sampleIndexes[medoidsSampleIndexes]
    medoidsHistograms = sample[medoidsSampleIndexes]
    
#    outputPath = outputDir / f"{street}_medoids.npz"
#    np.savez(outputPath, histograms = medoidsHistograms, indices = medoidsIndexes)

    assign_clusters(
        csvFiles,
        medoidsHistograms,
        histogramsChunksize,
        outputDir / f"{street}_buckets.csv"
    )

    end = time.perf_counter()
    print(f"Elapsed: {end - start:.1f} s")

def main():
    cluster_model("river")

if __name__ == "__main__":
    main()