LogData = importdata('logfile.dat');
Time = LogData.data(:,1);
Window = LogData.data(:,2);
OutputByterate = LogData.data(:,3);
OutputDataByterate = LogData.data(:,4);
OutputInstantByterate = LogData.data(:,5);
OutputInstantThroughput = LogData.data(:,6);

figure('Name', 'Window size', 'NumberTitle', 'off');
plot(Time, Window);
xlabel('Time (s)');
ylabel('Window size (bytes)');

figure('Name', 'Byte rate', 'NumberTitle', 'off');
plot(Time, Byterate);
xlabel('Time (s)');
ylabel('Byte rate (raw output)');

figure('Name', 'Throughput', 'NumberTitle', 'off');
plot(Time, Throughput);
xlabel('Time (s)');
ylabel('Data rate (raw data output)');

figure('Name', 'Instant byte rate', 'NumberTitle', 'off');
plot(Time, InstantByterate);
xlabel('Time (s)');
ylabel('Instant byterate (raw output)');

figure('Name', 'Instant throughput', 'NumberTitle', 'off');
plot(Time, InstantThroughput);
xlabel('Time (s)');
ylabel('Instant data rate (raw data output)');
