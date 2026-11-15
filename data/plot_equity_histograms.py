import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
from pathlib import Path

NUM_FLOP_BOARDS = 1286792
NUM_TURN_BOARDS = 13960050
NUM_RIVER_BOARDS = 123156254

def get_cvs_files(
        street: str
):

    # csv histogram directory
    dir = Path(__file__).resolve().parent / "histograms" / f"{street}_histograms"

    # sort and store the csv files
    csvFiles = sorted(
        dir.glob(f"{street}_equity_histogram_*.csv"),
        key = lambda path: path.stem.rsplit("_", 1)[1] # sort
    )

    return csvFiles

def plot_histogram(
        index: int,
        street: str,
):

    csvFiles = get_cvs_files(street)
    if street == "flop":
        totalNumHistograms = NUM_FLOP_BOARDS
    if street == "turn":
        totalNumHistograms = NUM_TURN_BOARDS
    if street == "river":
        totalNumHistograms = NUM_RIVER_BOARDS

    numHistogramsPerFile = totalNumHistograms // len(csvFiles)
    csvFile = csvFiles[index // numHistogramsPerFile]
    localCSVIndex = index % numHistogramsPerFile
    row = pd.read_csv(
        csvFile,
        skiprows = range(1, localCSVIndex + 1),
        nrows = 1
    )
    histogram = row.iloc[0, 1:].to_numpy(dtype = np.float32)

    numbins = len(histogram)
    binwidth = 1 / numbins
    bincenters = np.arange(numbins) * binwidth + binwidth / 2

    [fig, ax] = plt.subplots(figsize = (10, 5))
    ax.bar(bincenters, histogram, width = binwidth)
    ax.set(
        title = f"Equity histogram ({street} index: {index})",
        xlabel = "Equity",
        ylabel = "Probability",
        xlim = (0,1),
        xticks = np.linspace(0, 1, 21),
        ylim = (0,1),
        yticks = np.linspace(0, 1, 3)
    )
    fig.tight_layout()
    plt.show()