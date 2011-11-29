RawLogData = importdata('raw_output.dat');
RawTime = RawLogData.data(:,1);
Window = RawLogData.data(:,2);
OutputByterate = RawLogData.data(:,3);
OutputDataByterate = RawLogData.data(:,4);
OutputInstantByterate = RawLogData.data(:,5);
OutputInstantDataByterate= RawLogData.data(:,6);
RoundTripTime = RawLogData.data(:,7);

figure('Name', 'Window size', 'NumberTitle', 'off');
plot(RawTime, Window);
xlabel('Time (s)');
ylabel('Window size (bytes)');

figure('Name', 'Byte rate', 'NumberTitle', 'off');
plot(RawTime, OutputByterate);
xlabel('Time (s)');
ylabel('Byte rate (raw output)');

figure('Name', 'Throughput', 'NumberTitle', 'off');
plot(RawTime, OutputDataByterate);
xlabel('Time (s)');
ylabel('Data rate (raw data output)');

figure('Name', 'Instant byte rate', 'NumberTitle', 'off');
plot(RawTime, OutputInstantByterate);
xlabel('Time (s)');
ylabel('Instant byterate (raw output)');

figure('Name', 'Instant throughput', 'NumberTitle', 'off');
plot(RawTime, OutputInstantDataByterate);
xlabel('Time (s)');
ylabel('Instant data rate (raw data output)');

figure('Name', 'Round trip time', 'NumberTitle', 'off');
plot(RawTime, RoundTripTime);
xlabel('Time (s)');
ylabel('Time (ns)');

GoodputLogData = importdata('goodput.dat');
GoodTime = GoodputLogData(:,1);
GoodByterate = GoodputLogData(:,2);

figure('Name', 'Goodput', 'NumberTitle', 'off');
plot(GoodTime, GoodByterate);
xlabel('Time (s)');
ylabel('Goodput (Bps)');
