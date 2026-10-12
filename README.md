# MeritSeat — College Admission Management System Using Priority Queues

A local admission simulator for a DSA project. The admissions engine, including the binary max-heap priority queue, is written in **C**. HTML, CSS and JavaScript provide the browser interface. The C program itself also serves the dashboard over localhost and has a console mode.

## Run on macOS or Linux

From the project folder:

```sh
make
./admission
```

Open **http://127.0.0.1:8080**. Stop with Ctrl+C. To use another port, run `./admission --port 8081`. To demonstrate the same engine in a terminal, run `./admission --console`.

On macOS, install Apple's Command Line Tools if `make` or `cc` is missing: `xcode-select --install`.

## Admission workflow

1. Register applicants with entrance score (0–100), Class 12 percentage (0–100), Class 10 percentage (0–100), and one preferred department. If the workspace is empty, **Try sample applicants** loads six fictional records for a quick demonstration.
2. Choose a department. The **Waiting in merit order** list shows exactly who would be selected next; it is produced by extracting from a copy of the C heap.
3. Set the department's seat capacity, then click **Allocate**. New departments start with zero seats; the sample data button sets example capacities of 2 and 1. The C engine repeatedly extracts its highest-priority waiting applicant until the vacancies are filled.
4. Any remaining applicants stay in that department's priority queue. They are not automatically admitted just because a seat is vacant: allocation is an explicit action.
5. Cancelling an admitted seat automatically promotes the highest-priority waiting applicant for that department.

Eight departments have independent heaps: Computer Science, Electronics, Mechanical, Civil, Information Technology, Electrical & Electronics, AI & Data Science, and Biotechnology. The rule is higher entrance score, then higher Class 12 percentage, then higher Class 10 percentage, then earlier application order. This is a **demonstration policy**, not a real college admissions policy. Allocation works in rounds: a later applicant joins the waiting queue and cannot displace a seat already allocated. Applicant IDs and input order are assigned by the C engine. Records and seat capacities are saved to `data/admissions.tsv` after every change and reloaded when the program starts.

Older `ADMISSIONS1` files (four departments, no Class 10 field) are read automatically and saved in the new format after the next change. Their Class 10 value appears as **Missing** until you enter the actual mark from the application records. An unknown mark ranks below a recorded Class 10 mark if entrance and Class 12 marks tie. No marks are invented during migration.

## DSA explanation

The heap is an array of references (indices) to waiting applicants. For index `i`, its children are `2*i+1` and `2*i+2`. `heap_push` inserts and bubbles up; `heap_pop` extracts the root and bubbles down. Each operation is **O(log n)** and peeking at the root is **O(1)**. Allocating `k` seats costs **O(k log n)**, plus a scan of admitted records to count occupied seats. The heap array is not a completely sorted merit list. The C engine derives the list shown to users by repeatedly extracting from a **copy** of the heap, preserving the real queue. The expandable DSA section shows the actual heap array in levels.

Applicant search and occupied seat counts use a linear scan, **O(n)**. Cancelling an admitted seat scans for the ID, changes its status, and fills the vacancy from the heap. The `data/` folder is excluded from Git so local student records are not committed by accident.

## Files

- `src/main.c`: applicant records, priority queues, persistence, HTTP server, console interface.
- `web/`: dashboard UI; it calls the C HTTP endpoints and does not rank applicants.
- `tests/integration.py`: end-to-end check of ties, seat allocation, waiting order, cancellation promotion, and persistence.

Run `make test` to compile and check the system. The test uses a temporary data file and a random available localhost port, leaving your records intact. In environments that prohibit local sockets, it runs the C console checks and reports that the HTTP check was skipped.

## Viva demonstration

Start with an empty workspace and click **Try sample applicants**. Computer Science has two seats. Sara and Mira both scored **96** in the entrance exam and **90%** in Class 12; Sara's **92%** in Class 10 outranks Mira's **89%**. Press **Allocate 2 seats**: Sara and Mira are admitted; Kiran and Asha wait. Cancel Sara's seat and Kiran is promoted. For an exact tie in all three marks, the earlier application goes first.

## Current limits

The prototype supports 512 total applicants and one department choice per applicant. Scores are integer values between 0 and 100. It is a local, single-user demonstration with no sign-in or real admissions policy. Run it from the project directory so it can find `web/` and `data/`.

## Hosted demo

[![Deploy to Render](https://render.com/images/deploy-to-render-button.svg)](https://render.com/deploy?repo=https://github.com/varun-tg-cs/meritseat)

Click the button, sign in to Render, review the single free web service, and approve the deployment. The Dockerfile and `render.yaml` contain the configuration. See [DEPLOY_RENDER.md](DEPLOY_RENDER.md). The free tier's storage is temporary, so it is for fictional demo records only.
