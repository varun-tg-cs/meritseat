# Deploy MeritSeat on Render

This folder contains the C backend and web UI. The Dockerfile builds the C server and serves the app on Render's assigned PORT, bound to 0.0.0.0. No Windows executable is involved in deployment.

## Publish

1. Upload the contents of this folder to a GitHub repository. Do not upload private student records; the `data/` folder is excluded from Git.
2. In Render, choose **New > Web Service**, connect that repository, and select **Docker** as the runtime. The root directory should be the repository root; `Dockerfile` is there.
3. Choose the **Free** plan for a temporary classroom demo and create the service. Alternatively, use **New > Blueprint** with `render.yaml` in the repository.
4. Once deployment finishes, use the HTTPS `onrender.com` link from the Render dashboard on any device. If the service has slept, the first request can take about a minute.
5. On a fresh workspace, click **Try sample applicants**, then choose Computer Science, **Allocate 2 seats**, and cancel an admitted seat to show promotion.

## Data and access

The free Render filesystem is temporary. Changes disappear when the service restarts, redeploys, or sleeps. This app has no sign-in; anyone with the public link can add or change demonstration records. Use fictional data only.

For lasting records, use a paid Render web service with a persistent disk mounted at `/var/data`, and set the environment variable `DATA_DIR=/var/data`. The app writes `admissions.tsv` there. Even then, add authentication before storing actual student information or sharing the link widely.

To keep the app local, run `make && ./admission` and open `http://127.0.0.1:8080`.
