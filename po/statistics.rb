#!/usr/bin/env ruby -w

require 'open3'

d = Dir.new('.')
results = []
=begin Single-threaded version
d.each do |file|
  next unless file =~ /.po$/
  results << [file.split('.')[0], Open3.popen3("msgfmt --stat #{file}") {|sin, sout, serr| serr.readlines[0]}]
end
=end

#multi-threaded version, theoretically number_of_cores times faster. In
#practice, Ruby lacks proper OS thread support, so it'll be just as slow --
#WRONG! Although Ruby runs in a single thread (supposedly), all those
#msgfmts are taking up the huge majority of CPU time, and THEY run in
#separate threads. This still results in double speedups.
thr = []
d.each do |file|
  next unless file =~ /.po$/
  name = file.split('.')[0]
  thr << Thread.new { results << [name, Open3.popen3("msgfmt --stat #{file}") {|sin, sout, serr| serr.readlines[0]}] }
end
thr.each {|i| i.join}

results.sort! {|a, b| b[1].split(' ')[0].to_i <=> a[1].split(' ')[0].to_i}
results.each {|i| puts "#{i[0]}: #{i[1]}"}
